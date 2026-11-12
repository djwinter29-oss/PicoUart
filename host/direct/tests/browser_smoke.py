# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
import os
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1] / "src"


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, _format: str, *_args: object) -> None:
        pass


FAKE_HID = r"""
(() => {
  const reads = [];
  let writes = 0;
  let channelState = 2;
  function report(id, sequence = 1) {
    const size = id === 1 ? 27 : id === 2 ? 36 : id === 3 ? 20
      : id === 20 ? 4 : id >= 30 ? 24 : 30;
    const bytes = new Uint8Array(size);
    const data = new DataView(bytes.buffer);
    bytes[0] = 1;
    if (id === 1) {
      data.setUint16(1, 1, true);
      bytes.fill(0x71, 3, 11);
      bytes[11] = 1;
      bytes[14] = 2;
      bytes[15] = 3;
      bytes[16] = 63;
      bytes[17] = 7;
      bytes[18] = 31;
      data.setUint32(19, 524288, true);
      data.setUint32(23, 32768, true);
    } else if (id === 2) {
      bytes[1] = 2;
      bytes[2] = 6;
      bytes.fill(255, 3, 27);
      bytes[27] = 1;
      for (const [offset, capacity] of [[28, 4096], [30, 4096], [32, 2048], [34, 2048]]) {
        data.setUint16(offset, capacity, true);
      }
    } else if (id === 3) {
      data.setBigUint64(4, 100000n, true);
      data.setUint32(16, 150000000, true);
    } else if (id >= 10 && id <= 15) {
      bytes[1] = channelState;
      bytes[2] = 1;
      data.setUint32(3, 115200, true);
      bytes[7] = 8;
      bytes[8] = 1;
      data.setUint16(10, sequence, true);
      data.setUint16(12, 500, true);
      for (let offset = 14; offset < 30; offset += 4) {
        data.setUint32(offset, 4000000000 + sequence * 1000, true);
      }
    } else if (id >= 30) {
      bytes[1] = 1;
    }
    return new DataView(bytes.buffer);
  }
  class FakeDevice extends EventTarget {
    constructor() {
      super();
      this.vendorId = 0x2e8a;
      this.productId = 0x116b;
      this.opened = false;
      this.collections = [{
        featureReports: [{ reportId: 1, items: [{ reportSize: 8, reportCount: 27 }] }],
      }];
    }
    async open() { this.opened = true; }
    async close() { this.opened = false; }
    async receiveFeatureReport(id) {
      reads.push(id);
      return report(id);
    }
    async sendFeatureReport() {
      writes++;
      throw new Error("The browser smoke test must not send board controls");
    }
    emit(sequence) {
      for (let id = 10; id <= 15; id++) {
        const event = new Event("inputreport");
        Object.defineProperties(event, {
          device: { value: this },
          reportId: { value: id },
          data: { value: report(id, sequence) },
        });
        this.dispatchEvent(event);
      }
    }
    setChannelState(state) { channelState = state; }
  }
  const device = window.reviewDevice = new FakeDevice();
  const hid = new EventTarget();
  hid.requestDevice = async () => [device];
  Object.defineProperty(navigator, "hid", { configurable: true, value: hid });
  window.reviewReads = reads;
  window.reviewWrites = () => writes;
  const nativeSetInterval = window.setInterval.bind(window);
  window.reviewIntervals = [];
  window.setInterval = (...args) => {
    const id = nativeSetInterval(...args);
    window.reviewIntervals.push(id);
    return id;
  };
})();
"""


def main() -> None:
    handler = partial(QuietHandler, directory=str(ROOT))
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with sync_playwright() as playwright:
            options = {}
            if executable := os.environ.get("PLAYWRIGHT_CHROMIUM_EXECUTABLE"):
                options["executable_path"] = executable
            browser = playwright.chromium.launch(**options)
            page = browser.new_page(viewport={"width": 1440, "height": 1100})
            page_errors = []
            page.on("pageerror", lambda error: page_errors.append(str(error)))
            page.add_init_script(FAKE_HID)
            page.goto(f"http://127.0.0.1:{server.server_port}/")
            page.locator("#connect").click()
            page.wait_for_function(
                "() => document.querySelector('#serial').textContent === '7171717171717171' "
                "&& window.reviewReads.includes(35)"
            )
            page.evaluate(
                "() => { clearInterval(window.reviewIntervals[0]); "
                "window.reviewSequence = 3; "
                "for (let sequence = 1; sequence <= 3; sequence++) window.reviewDevice.emit(sequence); }"
            )
            assert page.locator("#channel-rows tr").count() == 6
            assert page.locator("#channel-rows tr").first.locator(".traffic b").first.text_content() == "2.0 KB/s"

            for width in (1440, 1200, 1150, 1120, 1110, 1101, 1100, 1024, 720, 390):
                page.set_viewport_size({"width": width, "height": 1100})
                page.evaluate("() => window.reviewDevice.emit(++window.reviewSequence)")
                totals = page.locator("#channel-rows .traffic-total").all_text_contents()
                numeric_totals = [int("".join(character for character in total if character.isdigit()))
                                  for total in totals if any(character.isdigit() for character in total)]
                assert len(numeric_totals) == 24 and min(numeric_totals) >= 4_000_000_000, (
                    f"Long counters missing at {width}px: {totals}"
                )
                layout = page.evaluate(
                    """() => ({
                      overflow: document.documentElement.scrollWidth > innerWidth + 1,
                      overlaps: [...document.querySelectorAll('#channel-rows tr')].filter(row =>
                        row.querySelector('.traffic-metrics').getBoundingClientRect().right
                          > row.cells[4].getBoundingClientRect().left + 1).length,
                    })"""
                )
                assert layout == {"overflow": False, "overlaps": 0}, f"Layout at {width}px: {layout}"

            page.set_viewport_size({"width": 1440, "height": 1100})
            page.locator("#reboot-board").click()
            assert page.locator("#reboot-dialog").evaluate("(dialog) => dialog.open")
            assert page.evaluate("document.activeElement.id") == "cancel-reboot"
            page.keyboard.press("Escape")
            assert not page.locator("#reboot-dialog").evaluate("(dialog) => dialog.open")
            page.locator("#reboot-board").click()
            page.locator("#cancel-reboot").click()

            page.locator("#channel-rows tr").first.get_by_role(
                "button", name="Diagnostics", exact=True
            ).click()
            page.wait_for_timeout(500)
            page.locator("#refresh-diagnostics").click()
            page.wait_for_function(
                "() => window.reviewReads.filter(id => id >= 30 && id <= 35).length >= 12"
            )
            page.wait_for_function("() => !document.querySelector('#refresh-diagnostics').disabled")
            page.wait_for_function(
                """() => {
                  const age = document.querySelector('.diagnostics-age').textContent;
                  const match = age.match(/^Diagnostics read (\\d+) s ago$/);
                  return match && Number(match[1]) >= 4;
                }""",
                timeout=7000,
            )
            assert "Telemetry stale" in page.locator("#sample-time").text_content()
            assert page.locator("#channel-rows tr").first.locator(".state").is_hidden()

            page.evaluate(
                "() => { window.reviewDevice.setChannelState(0); window.reviewDevice.emit(4); }"
            )
            assert page.locator("#channel-rows tr").first.locator(".traffic b").first.text_content() == "-- KB/s"
            assert not page.locator("#traffic-continuity-warning").is_hidden()
            page.locator("#close-channel-diagnostics").click()
            page.locator("#configuration-profile").select_option("0")
            page.locator("#save-configuration").click()
            assert page.locator("#configuration-dialog").evaluate("(dialog) => dialog.open")
            assert not page.locator("#confirm-configuration").is_disabled()
            page.wait_for_function(
                """() => document.querySelector('#save-configuration').disabled
                  && document.querySelector('#confirm-configuration').disabled""",
                timeout=5000,
            )
            page.locator("#confirm-configuration").evaluate("(button) => button.click()")
            assert page.evaluate("window.reviewWrites()") == 0
            assert not page_errors, f"Uncaught browser errors: {page_errors}"
            page.locator("#cancel-configuration").click()
            page.locator("#connect").click()
            page.wait_for_function(
                "() => document.querySelector('#connection-label').textContent === 'Disconnected'"
            )
            browser.close()
            print("Direct Dashboard Chromium smoke passed with simulated WebHID; no board controls were sent.")
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    main()
