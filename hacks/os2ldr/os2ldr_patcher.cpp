/* $Id: $ */
/** @file
 * Bird's OS2LDR patch utility.
 */

/*
 * Copyright (c) 2011 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 * All Rights Reserved.
 *
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


static int failed(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat,  va);
    va_end(va);
    return 1;
}


static char *readfile(const char *pszFile, struct stat const *pSt)
{
    char *pb = (char *)calloc(pSt->st_size + 16, 1);
    if (pb)
    {
        errno = 0;
        FILE *pFile = fopen(pszFile, "rb");
        if (pFile)
        {
            errno = 0;
            int rc = fread(pb, (unsigned)pSt->st_size, 1, pFile);
            fclose(pFile);
            if (rc == 1)
                return pb;
            failed("error: fread failed on '%s': %s\n",  pszFile,  strerror(errno));
        }
        else
            failed("error: failed to open '%s': %s\n", pszFile, strerror(errno));

        free(pb);
    }
    else
        failed("error: out of memory!\n");
    return NULL;

}


static void putu16(char *pb, unsigned uVal)
{
    pb[0] = uVal & 0xff;
    pb[1] = uVal >> 8;
}


static unsigned getu16(char const *pb)
{
    unsigned u = *(unsigned char const *)(pb + 1);
    u <<= 8;
    u |= *(unsigned char const *)pb;
    return u;
}


static int validatepatch(char const *pbPatch, unsigned cbPatch, char const *pbInput, unsigned cbInput)
{
    if (strncmp(pbPatch, "bird", 4))
        return failed("error: Bad patch signature!\n");

    unsigned uOrg = getu16(&pbPatch[4]);
    if (uOrg <= cbInput)
        return failed("error: The patch origin is lower or equal to the OS2LDR size: %#u <= %#u\n", uOrg, cbInput);
    if (uOrg > 0xe0000)
        return failed("error: The patch origin is too high: %#x\n", uOrg);

    unsigned cbTotal = uOrg + cbPatch;
    cbTotal += 0x00001fff;              /* Arena + alignment */
    cbTotal &= 0xfffff000;
    if (cbTotal > 0xf000)
        return failed("error: The patched OS2LDR is too large: %#x (%u), max 0xf000\n", cbTotal, cbTotal);

    /*
     * Verify the size patches.
     */
    const char *pb = &pbPatch[6];
    if (strncmp(pb, "size", 4))
        return failed("error: Bad patch header (size)!\n");
    pb += 4;
    unsigned cSizes = getu16(pb);
    pb += 2;

    while (cSizes-- > 0)
    {
        unsigned offSize  = getu16(pb);
        pb += 2;
        unsigned uOrgSize = getu16(pb);
        pb += 2 + 2;
        if (offSize + 2 > cbInput)
            return failed("error: Size patch at %#x is beyond the end of the input file (%#x)!\n", offSize, cbInput);

        unsigned u = getu16(pbInput + offSize);
        if (u != uOrgSize)
            return failed("error: Size patch at %#x states a different value (%#x) than the input (%#x)!\n", offSize, uOrgSize, u);
    }

    /*
     * Verify the jmp patches.
     */
    if (strncmp(pb, "jmps", 4))
        return failed("error: Bad patch header (jmps)!\n");
    unsigned cJmps = getu16(pb + 4);
    pb += 6;

    for (unsigned iJmp = 0; iJmp < cJmps; iJmp++)
    {
        unsigned offJmp   = getu16(pb + 0);
        unsigned offDst   = getu16(pb + 2);
        unsigned cbLeadIn = getu16(pb + 4);
        pb += 6;
        if (cbLeadIn >= 16)
            return failed("error: Jmp patch #%u at %#x: too many lead in bytes: %#x\n", iJmp, offJmp, cbLeadIn);
        if (offJmp + cbLeadIn > cbInput || offJmp >= cbInput)
            return failed("error: Jmp patch #%u at %#x is beyond the end of the input file (%#x)!\n", iJmp, offJmp, cbInput);
        if (offDst < uOrg || offDst >= uOrg + cbPatch)
            return failed("error: Jmp patch #%u at %#x destination is out of range: %#x\n", iJmp, offJmp, offDst);
        if (memcmp(pbInput + offJmp, pb, cbLeadIn))
        {
            failed("error: Jmp patch #%u at %#x states other lead in bytes than the input\n", iJmp, offJmp);
            for (unsigned off = 0; off < cbLeadIn; off++)
                fprintf(stderr, "  %#x+%u: %02x %02x\n", offJmp, off, (unsigned char)pb[off], (unsigned char)pbInput[offJmp + off]);
            return 1;
        }
        pb += cbLeadIn + 3;
    }

    /* The end */
    if (strncmp(pb, "end", 4))
        return failed("error: Bad patch header (end)!\n");

    return 0;
}


static int linkpatch(char *pbOutput, unsigned *pcbOutput, char const *pbInput, unsigned cbInput,
                     char const *pbPatch, unsigned cbPatch)
{
    if (strncmp(pbPatch, "bird", 4))
        return failed("error: Doofus - bird!\n");

    /* Figure the size. */
    unsigned const offPatch = getu16(&pbPatch[4]);
    printf("offPatch=%#x\n", offPatch);
    *pcbOutput = cbPatch + offPatch;

    /* Link the two input binaries. */
    memset(pbOutput, 0, *pcbOutput);
    memcpy(pbOutput, pbInput, cbInput);
    memcpy(pbOutput + offPatch, pbPatch, cbPatch);

    /*
     * Apply size patches
     */
    const char *pb = pbPatch + 6;
    if (strncmp(pb, "size", 4))
        return failed("error: Doofus - size!\n");
    unsigned cSizes = getu16(pb + 4);
    pb += 6;

    while (cSizes-- > 0)
    {
        unsigned offSize  = getu16(pb);
        signed   iDelta   = getu16(pb + 4);
        pb += 6;
        putu16(pbOutput + offSize, *pcbOutput + iDelta);
    }

    /*
     * Apply the jmp patches.
     */
    if (strncmp(pb, "jmps", 4))
        return failed("error: Doofus - jmps!\n");
    unsigned cJmps = getu16(pb + 4);
    pb += 6;

    while (cJmps-- > 0)
    {
        unsigned offJmp   = getu16(pb + 0);
        unsigned offDst   = getu16(pb + 2);
        unsigned cbLeadIn = getu16(pb + 4);
        pb += 6 + cbLeadIn + 3;
#if 0 /* debug */
        pbOutput[offJmp++] = (char)0xf4;  /* hlt */
        pbOutput[offJmp++] = (char)0xf4;  /* hlt */
        pbOutput[offJmp++] = (char)0xf4;  /* hlt */
        pbOutput[offJmp++] = (char)0xf4;  /* hlt */
        pbOutput[offJmp++] = (char)0xf4;  /* hlt */
        pbOutput[offJmp++] = (char)0xf4;  /* hlt */
        pbOutput[offJmp++] = (char)0xeb;  /* jmp $-6 */
        pbOutput[offJmp++] = (char)-6;
#endif
        pbOutput[offJmp++] = (char)0x90;  /* NOP */
        pbOutput[offJmp++] = (char)0x90;  /* NOP */
        pbOutput[offJmp++] = (char)0x90;  /* NOP */
        pbOutput[offJmp++] = (char)0xe9;  /* jmp rel16 */
        putu16(&pbOutput[offJmp], offDst - offJmp - 2);
    }

    /* The end */
    if (strncmp(pb, "end", 4))
        return failed("error: Doofus - end!\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4)
        return failed("syntax error: %s <os2ldr> <patch-binary> <output>\nargc=%d\n", argv[0], argc);

    const char *pszInput  = argv[1];
    const char *pszPatch  = argv[2];
    const char *pszOutput = argv[3];

    /*
     * Check file existences and get the sizes of the the inputs.
     */
    struct stat StInput, StPatch;
    if (stat(pszOutput, &StInput) == 0)
        return failed("error: The output file '%s' exists already.\n", pszOutput);
    if (errno != ENOENT)
        return failed("error: Expected errno=%d (ENOENT), got %d: %s\n", ENOENT, errno, strerror(errno));

    if (stat(pszInput, &StInput) != 0)
        return failed("error: stat(%s) -> %d: %s\n", pszInput, errno, strerror(errno));
    if (stat(pszPatch, &StPatch) != 0)
        return failed("error: stat(%s) -> %d: %s\n", pszPatch, errno, strerror(errno));

    if (StInput.st_size >= 0xe000)
        return failed("error: %s is too big! %u bytes\n", pszInput, (unsigned)StInput.st_size);
    if (StPatch.st_size >= 0x2000)
        return failed("error: %s is too big! %u bytes\n", pszOutput, (unsigned)StInput.st_size);
    if (StInput.st_size + StPatch.st_size >= 0xf000)
        return failed("error: the input files are too big! %u bytes\n", (unsigned)(StInput.st_size + StPatch.st_size));

    /*
     * Read the input files.
     */
    char *pbInput = readfile(pszInput, &StInput);
    if (!pbInput)
        return 1;

    char *pbPatch = readfile(pszPatch, &StPatch);
    if (!pbInput)
        return 1;

    /*
     * Validate the patch and construct the output file.
     */
    int rc = validatepatch(pbPatch, (unsigned)StPatch.st_size, pbInput, (unsigned)StInput.st_size);
    if (rc)
        return rc;

    char *pbOutput = (char *)malloc(0x10000);
    if (!pbOutput)
        return failed("error: out of memory\n");

    unsigned cbOutput = 0;
    rc = linkpatch(pbOutput, &cbOutput, pbInput, (unsigned)StInput.st_size, pbPatch, (unsigned)StPatch.st_size);
    if (rc)
        return rc;

    /*
     * Write it to the output file.
     */
    errno = 0;
    FILE *pFile = fopen(pszOutput, "wb");
    if (!pFile)
        return failed("error: Failed to create output file '%s': %s\n", pszOutput, strerror(errno));
    rc = fwrite(pbOutput, cbOutput, 1, pFile);
    if (rc != 1 || fclose(pFile) != 0)
        return failed("error: Error writing output file: %s\n", strerror(errno));

    printf("Successfully created '%s'\n", pszOutput);
    return 0;
}

