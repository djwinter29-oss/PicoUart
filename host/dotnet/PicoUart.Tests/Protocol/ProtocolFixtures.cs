// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Buffers.Binary;
using PicoUart.Protocol;

namespace PicoUart.Tests.Protocol;

internal static class ProtocolFixtures
{
    internal static byte[] IdentityPayload()
    {
        var data = new byte[27];
        data[0] = 1;
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(1), 1);
        Convert.FromHexString("0123456789ABCDEF").CopyTo(data.AsSpan(3));
        data[11] = 1; data[12] = 2; data[13] = 3;
        data[14] = 2; data[15] = 3; data[16] = 3;
        data[17] = 7; data[18] = 0x1f;
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(19), 532480);
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(23), 65536);
        return data;
    }

    internal static BoardIdentity Identity() => Codec.ParseIdentity(IdentityPayload());

    internal static byte[] ConfigurationPayload()
    {
        var data = new byte[36];
        data[0] = 1;
        data[1] = 2;
        data[2] = 6;
        byte[] pins = [0, 1, 3, 2, 4, 5, 7, 6, 8, 9, 255, 255, 12, 13, 255, 255, 16, 17, 255, 255, 20, 21, 255, 255];
        pins.CopyTo(data.AsSpan(3));
        data[27] = 2;
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(28), 4096);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(30), 4096);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(32), 2048);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(34), 2048);
        return data;
    }

    internal static ConfigurationStatus Configuration() => Codec.ParseConfiguration(ConfigurationPayload(), Identity());
}
