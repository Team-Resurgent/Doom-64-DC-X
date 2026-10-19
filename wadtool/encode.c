/*
 * adapted from https://github.com/Erick194/D64TOOL/blob/main/src/Lzlib.cpp
 */


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>     /* malloc, free, rand */
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define WINDOW_SIZE	4096
#define LENSHIFT 4		// this must be log2(LOOKAHEAD_SIZE)
#define LOOKAHEAD_SIZE	(1<<LENSHIFT)

typedef struct node_struct node_t;

///////////////////////////////////////////////////////////////////////////
//       IMPORTANT: FOLLOWING STRUCTURE MUST BE 16 BYTES IN LENGTH       //
///////////////////////////////////////////////////////////////////////////

struct node_struct
{
    unsigned char *pointer;
    node_t *prev;
    node_t *next;
    int	pad;
};

typedef struct list_struct
{
    node_t *start;
    node_t *end;
} list_t;

static list_t hashtable[256]; // used for faster encoding
static node_t hashtarget[WINDOW_SIZE]; // what the hash points to

//
//  Adds a node to the hash table at the beginning of a particular index
//  Removes the node in its place before.
//

void addnode(unsigned char *pointer)
{
    list_t *list;
    int targetindex;
    node_t *target;

    targetindex = (uintptr_t) pointer & ( WINDOW_SIZE - 1 );

    // remove the target node at this index

    target = &hashtarget[targetindex];
    if (target->pointer)
    {
        list = &hashtable[*target->pointer];
        if (target->prev)
        {
            list->end = target->prev;
            target->prev->next = 0;
        }
        else
        {
            list->end = 0;
            list->start = 0;
        }
    }

    // add a new node to the start of the hashtable list

    list = &hashtable[*pointer];

    target->pointer = pointer;
    target->prev = 0;
    target->next = list->start;
    if (list->start) {
        list->start->prev = target;
    }
    else {
        list->end = target;
    }
    list->start = target;
}

unsigned char *EncodeJaguar(unsigned char *input, int inputlen, int *size)
{
    int putidbyte = 0;
    unsigned char *encodedpos;
    int encodedlen;
    int i;
    int len;
    int numbytes, numcodes;
    int codelencount;
    unsigned char *window;
    unsigned char *lookahead;
    unsigned char *idbyte;
    unsigned char *output, *ostart;
    node_t *hashp;
    int lookaheadlen;
    int samelen;

    // initialize the hash table to the occurences of bytes
    for (i=0 ; i<256 ; i++)
    {
        hashtable[i].start = 0;
        hashtable[i].end = 0;
    }

    // initialize the hash table target 
    for (i=0 ; i<WINDOW_SIZE ; i++)
    {
        hashtarget[i].pointer = 0;
        hashtarget[i].next = 0;
        hashtarget[i].prev = 0;
    }

    // create the output
    ostart = output = (unsigned char *) malloc((inputlen * 9)/8+1);

    // initialize the window & lookahead
    lookahead = window = input;

    numbytes = numcodes = codelencount = 0;

    while (inputlen > 0)
    {
        // set the window position and size
        window = lookahead - WINDOW_SIZE;
        if (window < input) { window = input; }

        // decide whether to allocate a new id byte
        if (!putidbyte)
        {
            idbyte = output++;
            *idbyte = 0;
        }
        putidbyte = (putidbyte + 1) & 7;

        // go through the hash table of linked lists to find the strings
        // starting with the first character in the lookahead

        encodedlen = 0;
        lookaheadlen = inputlen < LOOKAHEAD_SIZE ? inputlen : LOOKAHEAD_SIZE;

        hashp = hashtable[lookahead[0]].start;
        while (hashp)
        {
            samelen = 0;
            len = lookaheadlen;
            while (len-- && hashp->pointer[samelen] == lookahead[samelen]) {
                samelen++;
            }
            if (samelen > encodedlen)
            {
                encodedlen = samelen;
                encodedpos = hashp->pointer;
            }
            if (samelen == lookaheadlen) { break; }

            hashp = hashp->next;
        }

        // encode the match and specify the length of the encoding
        if (encodedlen >= 3)
        {
            *idbyte = (*idbyte >> 1) | 0x80;
            *output++ = ((lookahead-encodedpos-1) >> LENSHIFT);
            *output++ = ((lookahead-encodedpos-1) << LENSHIFT) | (encodedlen-1);
            numcodes++;
            codelencount+=encodedlen;
        } else { // or just store the unmatched byte
            encodedlen = 1;
            *idbyte = (*idbyte >> 1);
            *output++ = *lookahead;
            numbytes++;
        }

        // update the hash table as the window slides
        for (i = 0; i < encodedlen; i++) {
            addnode(lookahead++);
        }

        // reduce the input size
        inputlen -= encodedlen;
    }

    // done with encoding- now wrap up

    // put the end marker on the file
    if (!putidbyte) {
        idbyte = output++;
        *idbyte = 1;
    }
    else {
        *idbyte = ((*idbyte >> 1) | 0x80) >> (7 - putidbyte);
    }

    *output++ = 0;
    *output++ = 0;

    *size = output - ostart;
    return ostart;
}

void DecodeJaguar(unsigned char *input, unsigned char *output)
{
    int getidbyte = 0;
    int len;
    int pos;
    int i;
    unsigned char *source;
    int idbyte;

    while (1) {
        // get a new idbyte if necessary
        if (!getidbyte)
			idbyte = *input++;

        getidbyte = (getidbyte + 1) & 7;

        if (idbyte & 1) {
            // decompress
            pos = *input++ << LENSHIFT;
            pos = pos | (*input >> LENSHIFT);
            source = output - pos - 1;
            len = (*input++ & 0xf)+1;

            if (len == 1)
				break;

            for (i = 0; i < len; i++)
                *output++ = *source++;
        } else {
            *output++ = *input++;
        }

        idbyte = idbyte >> 1;
    }
}

typedef unsigned char byte;
typedef struct {
    int dec_bit_count;
    int dec_bit_buffer;
    int enc_bit_count;
    int enc_bit_buffer;
    byte* ostart;
    byte* output;
    byte* istart;
    byte* input;
} buffers_t;
typedef struct {
    int offset;
    int incrBit;
    int unk1;
    int type;
} encodeArgs_t;

static short ShiftTable[6] = { 4, 6, 8, 10, 12, 14 };
static int offsetTable[12];
static int offsetMaxSize, windowSize;
static encodeArgs_t encArgs;

#define HASH_SIZE (1 << 14)

static short* d64_encoder_hashtable;
static short* d64_encoder_hashtarget;
static short* d64_encoder_hashNext;
static short* d64_encoder_hashPrev;

typedef struct
{
    int offset;
    int cpycnt;
    int cpyoffset;
    int vExtra;
} copydata_t;

static copydata_t CpyDataTmp = { 0, 0, 0 };
static short DecodeTable[2516];
static short array01[1258];
static buffers_t buffers;
static byte* window;
static int OVERFLOW_READ;
static int OVERFLOW_WRITE;

int d64_encoder_GetOutputSize(void)
{
    return (int)(uintptr_t)((uintptr_t)buffers.output - (uintptr_t)buffers.ostart);
}

int d64_encoder_GetReadSize(void)
{
    return (int)(uintptr_t)((uintptr_t)buffers.input - (uintptr_t)buffers.istart);
}

static int d64_encoder_ReadByte(void)
{
    if ((int)(buffers.input - buffers.istart) >= OVERFLOW_READ)
        return -1;
    return *buffers.input++;
}

static void d64_encoder_WriteByte(byte outByte)
{
    if ((int)(buffers.output - buffers.ostart) >= OVERFLOW_WRITE) {
        exit(EXIT_FAILURE);
    }
    *buffers.output++ = outByte;
}

static void d64_encoder_WriteBinary(int binary) // 8002D288
{
    buffers.enc_bit_buffer = (buffers.enc_bit_buffer << 1);
    if (binary != 0)
        buffers.enc_bit_buffer = (buffers.enc_bit_buffer | 1);
    buffers.enc_bit_count = (buffers.enc_bit_count + 1);
    if (buffers.enc_bit_count == 8)
    {
        d64_encoder_WriteByte((byte)buffers.enc_bit_buffer);
        buffers.enc_bit_count = 0;
    }
}

static int d64_encoder_ReadBinary(void) // 8002D2F4
{
    int resultbyte;
    resultbyte = buffers.dec_bit_count;
    buffers.dec_bit_count = (resultbyte - 1);
    if ((resultbyte < 1))
    {
        resultbyte = d64_encoder_ReadByte();
        buffers.dec_bit_buffer = resultbyte;
        buffers.dec_bit_count = 7;
    }
    resultbyte = (0 < (buffers.dec_bit_buffer & 0x80));
    buffers.dec_bit_buffer = (buffers.dec_bit_buffer << 1);
    return resultbyte;
}

static void d64_encoder_WriteCodeBinary(int binary, int shift) // 8002D364
{
    int i;
    i = 0;
    if (shift > 0)
    {
        do
        {
            d64_encoder_WriteBinary(binary & 1);
            binary = (binary >> 1);
        } while (++i != shift);
    }
}

static int d64_encoder_ReadCodeBinary(int byte) // 8002D3B8
{
    int shift;
    int i;
    int resultbyte;
    resultbyte = 0;
    i = 0;
    shift = 1;
    if (byte <= 0)
        return resultbyte;
    do
    {
        if (d64_encoder_ReadBinary() != 0)
            resultbyte |= shift;
        i++;
        shift = (shift << 1);
    } while (i != byte);
    return resultbyte;
}

static void d64_encoder_FlushBitBuffer(void)
{
    if (buffers.enc_bit_count > 0) {
        d64_encoder_WriteByte((byte)(buffers.enc_bit_buffer << (8 - buffers.enc_bit_count)) & 0xff);
    }
}

static void d64_encoder_InitTables(void)
{
    int evenVal, oddVal, incrVal, i;
    short* curArray;
    short* incrTbl;
    short* evenTbl;
    short* oddTbl;
    int* Tbl1, * Tbl2;
    encArgs.incrBit = 3;
    encArgs.unk1 = 0;
    encArgs.type = 0;
    buffers.dec_bit_count = 0;
    buffers.dec_bit_buffer = 0;
    buffers.enc_bit_count = 0;
    buffers.enc_bit_buffer = 0;
    curArray = &array01[(0 + 2)];
    incrTbl = &DecodeTable[(1258 + 2)];
    incrVal = 2;
    do {
        *incrTbl++ = (short)(incrVal / 2);
        *curArray++ = 1;
    } while (++incrVal < 1258);
    oddTbl = &DecodeTable[(629 + 1)];
    evenTbl = &DecodeTable[(0 + 1)];
    evenVal = 1;
    oddVal = 3;
    do
    {
        *oddTbl++ = (short)oddVal;
        oddVal += 2;
        *evenTbl++ = (short)(evenVal * 2);
        evenVal++;
    } while (evenVal < 629);
    incrVal = 0;
    i = 0;
    Tbl2 = &offsetTable[6];
    Tbl1 = &offsetTable[0];
    do {
        *Tbl1++ = incrVal;
        incrVal += (1 << (ShiftTable[i] & 0x1f));
        *Tbl2++ = incrVal - 1;
    } while (++i <= 5);
    offsetMaxSize = incrVal - 1;
    windowSize = offsetMaxSize + (64 - 1);
}

static void d64_encoder_CheckTable(int a0, int a1, int a2)
{
    int i;
    int idByte1;
    int idByte2;
    short* curArray;
    short* evenTbl;
    short* oddTbl;
    short* incrTbl;
    i = 0;
    evenTbl = &DecodeTable[0];
    oddTbl  = &DecodeTable[629];
    incrTbl = &DecodeTable[1258];
    idByte1 = a0;
    do {
        idByte2 = incrTbl[idByte1];
        array01[idByte2] = (array01[a1] + array01[a0]);
        a0 = idByte2;
        if (idByte2 != 1) {
            idByte1 = incrTbl[idByte2];
            idByte2 = evenTbl[idByte1];
            a1 = idByte2;
            if (a0 == idByte2) {
                a1 = oddTbl[idByte1];
            }
        }
        idByte1 = a0;
    } while (a0 != 1);
    if (array01[1] != 0x7D0) {
        return;
    }
    array01[1] >>= 1;
    curArray = &array01[2];
    do
    {
        curArray[3] >>= 1;
        curArray[2] >>= 1;
        curArray[1] >>= 1;
        curArray[0] >>= 1;
        curArray += 4;
        i += 4;
    } while (i != 1256);
}

static void d64_encoder_UpdateTables(int tblpos)
{
    int incrIdx;
    int evenVal;
    int idByte1;
    int idByte2;
    int idByte3;
    int idByte4;
    short* evenTbl;
    short* oddTbl;
    short* incrTbl;
    short* tmpIncrTbl;
    evenTbl = &DecodeTable[0];
    oddTbl  = &DecodeTable[629];
    incrTbl = &DecodeTable[1258];
    idByte1 = (tblpos + 0x275);
    array01[idByte1] += 1;
    if (incrTbl[idByte1] != 1)
    {
        tmpIncrTbl = &incrTbl[idByte1];
        idByte2 = *tmpIncrTbl;
        if (idByte1 == evenTbl[idByte2]) {
            d64_encoder_CheckTable(idByte1, oddTbl[idByte2], idByte1);
        }
        else {
            d64_encoder_CheckTable(idByte1, evenTbl[idByte2], idByte1);
        }
        do
        {
            incrIdx = incrTbl[idByte2];
            evenVal = evenTbl[incrIdx];
            if (idByte2 == evenVal) {
                idByte3 = oddTbl[incrIdx];
            }
            else {
                idByte3 = evenVal;
            }
            if (array01[idByte3] < array01[idByte1])
            {
                if (idByte2 == evenVal) {
                    oddTbl[incrIdx] = (short)idByte1;
                }
                else {
                    evenTbl[incrIdx] = (short)idByte1;
                }
                evenVal = evenTbl[idByte2];
                if (idByte1 == evenVal) {
                    idByte4 = oddTbl[idByte2];
                    evenTbl[idByte2] = (short)idByte3;
                }
                else {
                    idByte4 = evenVal;
                    oddTbl[idByte2] = (short)idByte3;
                }
                incrTbl[idByte3] = (short)idByte2;
                *tmpIncrTbl = (short)incrIdx;
                d64_encoder_CheckTable(idByte3, idByte4, idByte4);
                tmpIncrTbl = &incrTbl[idByte3];
            }
            idByte1 = *tmpIncrTbl;
            tmpIncrTbl = &incrTbl[idByte1];
            idByte2 = *tmpIncrTbl;
        } while (idByte2 != 1);
    }
}

static int d64_encoder_StartDecodeByte(void)
{
    int lookup;
    short* evenTbl;
    short* oddTbl;
    lookup = 1;
    evenTbl = &DecodeTable[0];
    oddTbl  = &DecodeTable[629];
    while (lookup < 0x275) {
        if (d64_encoder_ReadBinary() == 0) {
            lookup = evenTbl[lookup];
        }
        else {
            lookup = oddTbl[lookup];
        }
    }
    lookup = (lookup + -0x275);
    d64_encoder_UpdateTables(lookup);
    return lookup;
}

void d64_encoder_InsertNodeDirectory(int start)
{
    int hashKey = ((window[start % windowSize] ^ (window[(start + 1) % windowSize] << 4)) ^ (window[(start + 2) % windowSize] << 8)) & (HASH_SIZE - 1);
        if (d64_encoder_hashtable[hashKey] == -1) {
            d64_encoder_hashtarget[hashKey] = start;
            d64_encoder_hashNext[start] = -1;
        }
        else {
            d64_encoder_hashNext[start] = d64_encoder_hashtable[hashKey];
            d64_encoder_hashPrev[d64_encoder_hashtable[hashKey]] = start;
        }
        d64_encoder_hashtable[hashKey] = start;
        d64_encoder_hashPrev[start] = -1;
}

/*
========================
=
= DeleteNodeDirectory
= routine required for encoding
=
========================
*/

void d64_encoder_DeleteNodeDirectory(int start) // 8002DAD0
{
    int hashKey = ((window[start % windowSize] ^ (window[(start + 1) % windowSize] << 4)) ^ (window[(start + 2) % windowSize] << 8)) & (HASH_SIZE - 1);
        if (d64_encoder_hashtable[hashKey] == d64_encoder_hashtarget[hashKey]) {
            d64_encoder_hashtable[hashKey] = -1;
        }
        else {
            d64_encoder_hashNext[d64_encoder_hashPrev[d64_encoder_hashtarget[hashKey]]] = -1;
            d64_encoder_hashtarget[hashKey] = d64_encoder_hashPrev[d64_encoder_hashtarget[hashKey]];
        }
}

/*
========================
=
= FindMatch
= routine required for encoding
=
========================
*/

int d64_encoder_FindMatch(int start, int count) // 8002DC0C
{
    int encodedlen;
    int offset;
    int i;
    int samelen;
    int next;
    int curr;
    int encodedpos;
    int hashKey;

    encodedlen = 0;
    if (start == windowSize) {
        start = 0;
    }

    hashKey = ((window[start % windowSize] ^ (window[(start + 1) % windowSize] << 4)) ^ (window[(start + 2) % windowSize] << 8)) & (HASH_SIZE - 1);
        offset = d64_encoder_hashtable[hashKey];

    i = 0;
    while (offset != -1)
    {
        if (++i > count) {
            break;
        }

        if ((window[(start + encodedlen) % windowSize]) == (window[(offset + encodedlen) % windowSize]))
        {
            samelen = 0;
            curr = start;
            next = offset;

            while (window[curr] == window[next]) {
                if (samelen >= 64) {
                    break;
                }
                if (next == start) {
                    break;
                }
                if (curr == encArgs.incrBit) {
                    break;
                }
                ++samelen;
                if (++curr == windowSize) {
                    curr = 0;
                }
                if (++next == windowSize) {
                    next = 0;
                }
            }

            encodedpos = start - offset;
            if (encodedpos < 0) {
                encodedpos += windowSize;
            }
            encodedpos -= samelen;
            if ((encArgs.unk1) && (encodedpos > offsetTable[6])) {
                break;
            }

            if (encodedlen < samelen && encodedpos <= offsetMaxSize && (samelen > 3 || offsetTable[6 + (encArgs.type + 3)] >= encodedpos)) {
                encodedlen = samelen;
                encArgs.offset = encodedpos;
            }
        }
            offset = d64_encoder_hashNext[offset]; // try next in list
    }
    return encodedlen;
}

void d64_encoder_StartEncodeCode(int lookup)
{
    int lookupCode, lookupCheck;
    short* oddTbl;
    short* incrTbl;
    int binCnt = 0;
    byte binary[64];
    oddTbl = &DecodeTable[629];
    incrTbl = &DecodeTable[1258];
    lookupCode = lookup + 0x275;
    while (1) {
        if (lookupCode <= 1) { break; }
        lookupCheck = oddTbl[incrTbl[lookupCode]];
        binary[binCnt++] = (lookupCheck == lookupCode) ? 1 : 0;
        lookupCode = incrTbl[lookupCode];
    };
    while (binCnt) {
        d64_encoder_WriteBinary(binary[--binCnt]);
    }
    d64_encoder_UpdateTables(lookup);
}

unsigned char* EncodeD64(unsigned char* input, int inputlen, int* size) {
    int i, readPos, nodePos, looKupCode, byteData;
    int cpyCountNext, cpyCount, cntMin, cntMax;
    int deleteNode, skipCopy;
    unsigned char* output = (unsigned char*)malloc(inputlen * 2);
    d64_encoder_InitTables();
    OVERFLOW_READ = inputlen;
    OVERFLOW_WRITE = inputlen * 2;
    deleteNode = 0;
    skipCopy = 0;
    readPos = encArgs.incrBit;
    nodePos = 0;
    buffers.input = buffers.istart = input;
    buffers.output = buffers.ostart = output;
    d64_encoder_hashtable = (short*)malloc(HASH_SIZE * sizeof(short));
    d64_encoder_hashtarget = (short*)malloc(HASH_SIZE * sizeof(short));
    d64_encoder_hashNext = (short*)malloc(windowSize * sizeof(short));
    d64_encoder_hashPrev = (short*)malloc(windowSize * sizeof(short));
    memset(d64_encoder_hashNext, 0, windowSize * sizeof(short));
    memset(d64_encoder_hashPrev, 0, windowSize * sizeof(short));
    window = (byte*)malloc(windowSize * sizeof(byte));
    memset(window, 0, windowSize * sizeof(byte));
    for (i = 0; i < HASH_SIZE; i++) {
        d64_encoder_hashtable[i] = -1;
        d64_encoder_hashtarget[i] = -1;
    }
    for (i = 0; i < encArgs.incrBit; i++) {
        byteData = d64_encoder_ReadByte();
        if (byteData != -1) {
            d64_encoder_StartEncodeCode(byteData);
            window[i] = byteData;
        }
    }
    if (d64_encoder_GetReadSize() < OVERFLOW_READ) {
        for (i = 0; i < 64; i++) {
            byteData = d64_encoder_ReadByte();
            if (byteData >= 128) {
                encArgs.type = 1;
            }
            if (byteData != -1) {
                window[encArgs.incrBit++] = byteData;
            }
        }
    }
    cntMin = (encArgs.type == 1) ? 20 : 50;
    cntMax = (encArgs.type == 1) ? 200 : 1000;
    if (d64_encoder_GetReadSize() < OVERFLOW_READ) {
        while (readPos != encArgs.incrBit) {
            d64_encoder_InsertNodeDirectory(nodePos);
            if (!skipCopy) {
                cpyCountNext = d64_encoder_FindMatch(readPos + 1, cntMin);
                cpyCount = d64_encoder_FindMatch(readPos, cntMax);
                if (cpyCount >= 3 && cpyCount >= cpyCountNext) {
                    int count = cpyCount;
                    int ValExtra = encArgs.offset;
                    int Shift = 0x04;
                    if ((ValExtra >= offsetTable[1]) && (ValExtra < offsetTable[2]))
                    {
                        Shift = 0x06;
                        ValExtra -= offsetTable[1];
                    }
                    if ((ValExtra >= offsetTable[2]) && (ValExtra < offsetTable[3]))
                    {
                        Shift = 0x08;
                        ValExtra -= offsetTable[2];
                    }
                    if ((ValExtra >= offsetTable[3]) && (ValExtra < offsetTable[4]))
                    {
                        Shift = 0x0A;
                        ValExtra -= offsetTable[3];
                    }
                    if ((ValExtra >= offsetTable[4]) && (ValExtra < offsetTable[5]))
                    {
                        Shift = 0x0C;
                        ValExtra -= offsetTable[4];
                    }
                    if ((ValExtra >= offsetTable[5]))
                    {
                        Shift = 0x0E;
                        ValExtra -= offsetTable[5];
                    }
                    if (Shift == 0x04) { looKupCode = (0x0101 + (count - 3)); }
                    else if (Shift == 0x06) { looKupCode = (0x013F + (count - 3)); }
                    else if (Shift == 0x08) { looKupCode = (0x017D + (count - 3)); }
                    else if (Shift == 0x0A) { looKupCode = (0x01BB + (count - 3)); }
                    else if (Shift == 0x0C) { looKupCode = (0x01F9 + (count - 3)); }
                    else if (Shift == 0x0E) { looKupCode = (0x0237 + (count - 3)); }
                    d64_encoder_StartEncodeCode(looKupCode);
                    d64_encoder_WriteCodeBinary(ValExtra, Shift);
                    skipCopy = 1;
                }
                else {
                    d64_encoder_StartEncodeCode(window[readPos]);
                }
            }
            if (--cpyCount == 0) {
                skipCopy = 0;
            }
            if (++readPos == windowSize) {
                readPos = 0;
            }
            if (++nodePos == windowSize) {
                nodePos = 0;
            }
            if (d64_encoder_GetReadSize() < OVERFLOW_READ) {
                byteData = d64_encoder_ReadByte();
                if (byteData != -1) {
                    window[encArgs.incrBit++] = byteData;
                }
                if (encArgs.incrBit == windowSize) {
                    encArgs.incrBit = 0;
                    deleteNode = 1;
                }
            }
            if (deleteNode && d64_encoder_GetReadSize() < OVERFLOW_READ) {
                d64_encoder_DeleteNodeDirectory(encArgs.incrBit);
            }
        }
    }
    d64_encoder_StartEncodeCode(0x100);
    d64_encoder_FlushBitBuffer();
    *size = d64_encoder_GetOutputSize();
    free(d64_encoder_hashtable);
    free(d64_encoder_hashtarget);
    free(d64_encoder_hashNext);
    free(d64_encoder_hashPrev);
    free(window);
    return output;
}
