/* encrypt.h - encrypt and decrypt arbitrary-length strings using DES
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: encrypt.h,v 1.7 2012-02-22 18:55:24 greg Exp $
 */
#ifndef ENCRYPT_H
#define ENCRYPT_H

int do_encrypt(char *key, char *data, int len);
int do_decrypt(char *key, char *data, int len);

#endif
