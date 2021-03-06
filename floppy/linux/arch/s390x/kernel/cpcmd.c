/*
 *  arch/s390/kernel/cpcmd.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/ebcdic.h>

void cpcmd(char *cmd, char *response, int rlen)
{
        const int mask = 0x40000000L;
        char obuffer[128];
        int olen;

        olen = strlen(cmd);
        strcpy(obuffer, cmd);
        ASCEBC(obuffer,olen);

        if (response != NULL && rlen > 0) {
                asm volatile ("   lrag  2,0(%0)\n"
                              "   lgr   4,%1\n"
                              "   o     4,%4\n"
                              "   lrag  3,0(%2)\n"
                              "   lgr   5,%3\n"
                              "   sam31\n"
                              "   .long 0x83240008 # Diagnose 83\n"
                              "   sam64"
                              : /* no output */
                              : "a" (obuffer), "d" (olen),
                                "a" (response), "d" (rlen), "m" (mask)
                              : "2", "3", "4", "5" );
                EBCASC(response, rlen);
        } else {
                asm volatile ("   lrag  2,0(%0)\n"
                              "   lgr   3,%1\n"
                              "   sam31\n"
                              "   .long 0x83230008 # Diagnose 83\n"
                              "   sam64"
                              : /* no output */
                              : "a" (obuffer), "d" (olen)
                              : "2", "3"  );
        }
}

