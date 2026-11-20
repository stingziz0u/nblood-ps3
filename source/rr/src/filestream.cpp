//-------------------------------------------------------------------------
/*
Copyright (C) 2010-2020 EDuke32 developers and contributors
Copyright (C) 2020 sirlemonhead
This file is part of Rednukem.
Rednukem is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#include "filestream.h"
#include <stdlib.h>

namespace RedNukem {

static uint32_t const kBufferSize = 64 * 1024;

bool FileStream::Open(const char *fileName)
{
    file = kopen4loadfrommod(fileName, 0);
    if (file == -1)
    {
        // log error
        return false;
    }

    if (!buffer)
        buffer = new uint8_t[kBufferSize];

    bufferLength = bufferPosition = 0;
    bufferFileOffset = 0;

    return true;
}

bool FileStream::Is_Open()
{
    return file != -1;
}

void FileStream::Close()
{
    kclose(file);
    file = -1;

    delete[] buffer;
    buffer = nullptr;
    bufferLength = bufferPosition = 0;
}

// the next part of the file into the buffer: what's there, or -1
int32_t FileStream::Fill()
{
    bufferFileOffset += bufferLength;
    bufferPosition = 0;

    int32_t const nRead = kread(file, buffer, kBufferSize);
    bufferLength = nRead > 0 ? nRead : 0;

    return nRead;
}

int32_t FileStream::ReadBytes(uint8_t *data, uint32_t nBytes)
{
    uint32_t nCount = 0;

    while (nCount < nBytes)
    {
        if (bufferPosition >= bufferLength)
        {
            // a big read straight into place, past the buffer
            if (nBytes - nCount >= kBufferSize)
            {
                bufferFileOffset += bufferLength;
                bufferLength = bufferPosition = 0;

                int32_t const nRead = kread(file, data + nCount, nBytes - nCount);

                if (nRead > 0)
                {
                    nCount += nRead;
                    bufferFileOffset += nRead;
                }
                break;
            }

            if (Fill() <= 0)
                break;
        }

        uint32_t const nChunk = min(nBytes - nCount, bufferLength - bufferPosition);

        memcpy(data + nCount, buffer + bufferPosition, nChunk);
        bufferPosition += nChunk;
        nCount += nChunk;
    }

    if (nCount != nBytes) {
        return 0;
    }

    return (int32_t)nCount;
}

uint64_t FileStream::ReadUint64LE()
{
    uint64_t value = 0;
    ReadBytes((uint8_t *)&value, 8);
    return B_LITTLE64(value);
}

uint64_t FileStream::ReadUint64BE()
{
    uint64_t value = 0;
    ReadBytes((uint8_t *)&value, 8);
    return B_BIG64(value);
}

uint32_t FileStream::ReadUint32LE()
{
    uint32_t value = 0;
    ReadBytes((uint8_t *)&value, 4);
    return B_LITTLE32(value);
}

uint32_t FileStream::ReadUint32BE()
{
    uint32_t value = 0;
    ReadBytes((uint8_t *)&value, 4);
    return B_BIG32(value);
}

uint16_t FileStream::ReadUint16LE()
{
    uint16_t value = 0;
    ReadBytes((uint8_t *)&value, 2);
    return B_LITTLE16(value);
}

uint16_t FileStream::ReadUint16BE()
{
    uint16_t value = 0;
    ReadBytes((uint8_t *)&value, 2);
    return B_BIG16(value);
}

uint8_t FileStream::ReadByte()
{
    if (bufferPosition < bufferLength)
        return buffer[bufferPosition++];

    uint8_t value = 0;
    ReadBytes(&value, 1);
    return value;
}

int32_t FileStream::Seek(int32_t offset, SeekDirection direction)
{
    int32_t nTarget;

    if (kSeekStart == direction)
        nTarget = offset;
    else if (kSeekCurrent == direction)
        nTarget = GetPosition() + offset;
    else if (kSeekEnd == direction)
        nTarget = kfilelength(file) + offset;
    else
        return -1;

    // inside what the buffer holds: only the position moves
    if (nTarget >= bufferFileOffset && nTarget <= bufferFileOffset + (int32_t)bufferLength)
    {
        bufferPosition = nTarget - bufferFileOffset;
        return nTarget;
    }

    int32_t const nStatus = klseek(file, nTarget, SEEK_SET);

    if (nStatus >= 0)
    {
        bufferFileOffset = nStatus;
        bufferLength = bufferPosition = 0;
    }

    return nStatus;
}

int32_t FileStream::Skip(int32_t offset)
{
    return Seek(offset, kSeekCurrent);
}

int32_t FileStream::GetPosition()
{
    return bufferFileOffset + bufferPosition;
}

} // close namespace RedNukem
