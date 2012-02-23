/* encrypt.c - encrypt and decrypt arbitrary-length strings using DES
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: encrypt.c,v 1.10 2012-02-22 18:55:24 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: encrypt.c,v 1.10 2012-02-22 18:55:24 greg Exp $";

/* an API to encrypt and decrypt arbitrary-length strings using DES.
 * this is a higher level API then Linux "encrypt.[hc]", which is
 * inconvenient to use directly.
 *
 * this is an API that is usable generically and is not specific to the
 * cloud_hub project.
 *
 * The two functions defined in this API are:
 *
 *    int do_encrypt(char *key, char *data, int len)
 *
 *    int do_decrypt(char *key, char *data, int len)
 *
 * The data are encrypted in place.  The key can be any length.
 * Since DEC uses 56-bit keys, if the key is longer than 8 characters
 * the data are re-encrypted using successive adjacent blocks of 8
 * characters from the key.  The key is required to be null-terminated
 * and is assumed to be standard ascii (0 .. 127; high bit is ignored.)
 *
 * "len" (the length) must be divisible by 8.  Otherwise, encryption
 * is not done, and errno is assigned the value "EINVAL".
 *
 * on success, 0 is returned on failure, 1 is returned and errno is
 * set.
 *
 * this API is non-re-entrant.
 */

#define _XOPEN_SOURCE
#include <unistd.h>

#define _XOPEN_SOURCE
#include <stdlib.h>

#define _GNU_SOURCE
#include <crypt.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "encrypt.h"

/* convenience function to set breakpoints when debugging */
//static void bp1(void) {}

static char block[64];
static int next;

#ifdef UNIT_TEST
    #define DEBUG 1
#endif

#ifdef DEBUG
/* debug-print block as a sequenc of 64 bits */
static void print_block(void)
{
    int i;

    for (i = 0; i < 64; i++) {
        printf("%d", block[i]);
        if (i % 4 == 3) { printf(" "); }
    }
    printf("\n");
}

/* debug-print data as a sequnce of hex bytes */
static void print_data(char *data, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        printf("%x", 0xff & data[i]);
    }
    printf("\n");
}
#endif

/* map "c" into 8 bits and append them to the internal work buffer "block"
 * starting at *next.  update next so that it indicates where to put subsequent
 * information in block.
 */
static void char_to_block(char c, int *next)
{
    int i;

    for (i = 0; i < 8; i++) {
        block[(*next)++] = c & 0x1;
        c >>= 1;
    }
}

/* traslate the bit vector "block" to a string by packing successive
 * chunks of 8 bits into characters.
 */
static void block_to_string(char *chars, int len)
{
    int i, j;

    next = 0;
    for (i = 0; i < len; i++) {
        unsigned char c = 0;
        for (j = 0; j < 8; j++) {
            c = (c >> 1) | (block[next++] << 7);
        }
        chars[i] = c;
    }
}

/* translate up to 8 characters from key into internal block work buffer;
 * unpack low 7 bits of each character into successive elements of block.
 * stop early if null is encountered in key.
 * use "setkey()" to set the key for the underlying encrypt.[hc] API.
 */
static int key_to_block(char *key)
{
    int i;
    memset(block, 0, 64);

    next = 0;
    for (i = 0; i < 8; i++) {
        if (key[i] == '\0') { break; }
        char_to_block(key[i], &next);
    }

    errno = 0;
    setkey(block);

    if (errno == 0) { return 0; }
    else { return -1; }
}

/* to the actual encryption or decryption.  enc_dec == 0 --> decrypt,
 * enc_dec == 1 --> encrypt.
 */
static int do_enc_dec(char *key, char *data, int len, int enc_dec)
{
    int i, j;
    int key_start, key_index;
    int result = 0;

    /* for DES, it apparently improves the encryption to re-encrypt
     * over and over again using different 56-bit keys.  So, we take
     * the password, as long as it is, and re-encrypt using each
     * successive chunk of 8 characters.
     *
     * for encoding, read the key in chunks of 8 characters left to right,
     * with last chunk being 8 or less characters.
     *
     * for decoding, start with last chunk that may be less than 8 characters,
     * and then go right to left from there.
     */
    if (enc_dec) {
        /* decoding */
        if (strlen(key) == 0) {
            key_start = -1;
        } else {
            key_start = 8 * ((strlen(key) - 1) / 8);
        }
    } else {
        /* encoding */
        key_start = 0;
    }

    for (key_index = key_start;
        enc_dec ? (key_index >= 0) : (key_index < strlen(key));
        enc_dec ? (key_index -= 8) : (key_index += 8))
    {
        if (key_to_block(&key[key_index]) != 0) { result = -1; break; }

        for (i = 0; i < len; i += 8) {
            memset(block, 0, 64);

            next = 0;
            for (j = i; j < i + 8; j++) {
                if (j == len) { break; }
                char_to_block(data[j], &next);
            }
            #ifdef DEBUG
                print_block();
                print_data(&data[i], 8);
            #endif

            encrypt(block, enc_dec);

            block_to_string(&data[i], 8);

            #ifdef DEBUG
                print_block();
                print_data(&data[i], 8);
            #endif
        }
    }

    return result;
}

/* encrypt data in place, using null-terminated key.  len must be divisible
 * by 8, otherwise no encryption is done and errno is set to EINVAL.
 */
int do_encrypt(char *key, char *data, int len)
{
    if (len % 8 != 0) {
        errno = EINVAL;
        return -1;
    }
    return (do_enc_dec(key, data, len, 0));
}
/* decrypt data in place, using null-terminated key.  len must be divisible
 * by 8, otherwise no decryption is done and errno is set to EINVAL.
 */
int do_decrypt(char *key, char *data, int len)
{
    if (len % 8 != 0) {
        errno = EINVAL;
        return -1;
    }
    return (do_enc_dec(key, data, len, 1));
}

#ifdef UNIT_TEST
/* unit-test this API.  argv[1] is used as key, argv[2] is string to encrypt.
 * encrypt data, print bytes of encrypted data, then decrypt and print
 * decrypted string, which should match original argv[2].
 */
int main(int argc, char **argv)
{
    int len;
    char *buf;

    len = strlen(argv[2]) + 1;
    if (len % 8 != 0) { len += 8 - len % 8; }
    buf = malloc(len);
    memset(buf, 0, len);
    strcpy(buf, argv[2]);

    if (do_encrypt(argv[1], buf, len) != 0) {
        fprintf(stderr, "problem with encryption; errno:  %d\n", errno);
        return -1;
    }

    printf("encrypted data:  ");
    print_data(buf, len);

    if (do_decrypt(argv[1], buf, len) != 0) {
        fprintf(stderr, "problem with encryption; errno:  %d\n", errno);
        return -1;
    }


    printf("result '%s'\n", buf);

    free(buf);
    
    return 0;
}
#endif
