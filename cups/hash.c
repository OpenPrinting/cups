//
// Hashing functions for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2015-2019 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"
#include "md5-internal.h"
#ifdef HAVE_OPENSSL
#  include <openssl/evp.h>
#else // HAVE_GNUTLS
#  include <gnutls/crypto.h>
#endif // HAVE_OPENSSL


//
// Note: While both GNU TLS and OpenSSL offer HMAC functions, they also exclude
// certain hashes depending on the version of library and whatever patches are
// applied by the OS vendor/Linux distribution.  Since printers sometimes rely
// on otherwise deprecated/obsolete hash functions for things like PIN printing
// ("job-password"), and since such uses already have poor security regardless
// of the hash function used, it is more important to provide guaranteed
// implementations over some imaginary notion of "guaranteed security"...
//

//
// Local functions...
//

static ssize_t	hash_data(const char *algorithm, unsigned char *hash, size_t hashsize, const void *a, size_t alen, const void *b, size_t blen);


//
// 'cupsHashData()' - Perform a hash function on the given data.
//
// This function performs a hash function on the given data. The "algorithm"
// argument can be any of the registered, non-deprecated IPP hash algorithms for
// the "job-password-encryption" attribute, including "sha" for SHA-1,
// "sha2-256" for SHA2-256, etc.
//
// The "hash" argument points to a buffer of "hashsize" bytes and should be at
// least 64 bytes in length for all of the supported algorithms.
//
// The returned hash is binary data.
//

ssize_t					// O - Size of hash or -1 on error
cupsHashData(const char    *algorithm,	// I - Algorithm name
             const void    *data,	// I - Data to hash
             size_t        datalen,	// I - Length of data to hash
             unsigned char *hash,	// I - Hash buffer
             size_t        hashsize)	// I - Size of hash buffer
{
  if (!algorithm || !data || datalen == 0 || !hash || hashsize == 0)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad arguments to function"), 1);
    return (-1);
  }

  return (hash_data(algorithm, hash, hashsize, data, datalen, NULL, 0));
}


//
// 'cupsHashString()' - Format a hash value as a hexadecimal string.
//
// The passed buffer must be at least 2 * hashsize + 1 characters in length.
//

const char *				// O - Formatted string
cupsHashString(
    const unsigned char *hash,		// I - Hash
    size_t              hashsize,	// I - Size of hash
    char                *buffer,	// I - String buffer
    size_t		bufsize)	// I - Size of string buffer
{
  char		*bufptr = buffer;	// Pointer into buffer
  static const char *hex = "0123456789abcdef";
					// Hex characters (lowercase!)


  // Range check input...
  if (!hash || hashsize < 1 || !buffer || bufsize < (2 * hashsize + 1))
  {
    if (buffer)
      *buffer = '\0';
    return (NULL);
  }

  // Loop until we've converted the whole hash...
  while (hashsize > 0)
  {
    *bufptr++ = hex[*hash >> 4];
    *bufptr++ = hex[*hash & 15];

    hash ++;
    hashsize --;
  }

  *bufptr = '\0';

  return (buffer);
}


//
// 'cupsHMACData()' - Perform a HMAC function on the given data.
//
// This function performs a HMAC function on the given data with the given key.
// The "algorithm" argument can be any of the registered, non-deprecated IPP
// hash algorithms for the "job-password-encryption" attribute, including
// "sha" for SHA-1, "sha2-256" for SHA2-256, etc.
//
// The "hmac" argument points to a buffer of "hmacsize" bytes and should be at
// least 64 bytes in length for all of the supported algorithms.
//
// The returned HMAC is binary data.
//

ssize_t					// O - The length of the HMAC or `-1` on error
cupsHMACData(
    const char          *algorithm,	// I - Hash algorithm
    const unsigned char *key,		// I - Key
    size_t              keylen,		// I - Length of key
    const void          *data,		// I - Data to hash
    size_t              datalen,	// I - Length of data to hash
    unsigned char       *hmac,		// I - HMAC buffer
    size_t              hmacsize)	// I - Size of HMAC buffer
{
  size_t	i,			// Looping var
		b;			// Block size
  unsigned char	buffer[128],		// Intermediate buffer
		hash[128],		// Hash buffer
		hkey[128];		// Hashed key buffer
  ssize_t	hashlen;		// Length of hash


  // Range check input...
  if (!algorithm || !key || keylen == 0 || !data || datalen == 0 || !hmac || hmacsize < 32)
    return (-1);

  // Determine the block size...
  if (!strcmp(algorithm, "sha2-384") || !strncmp(algorithm, "sha2-512", 8))
    b = 128;
  else
    b = 64;

  // If the key length is larger than the block size, hash it and use that
  // instead...
  if (keylen > b)
  {
    if ((hashlen = hash_data(algorithm, hkey, sizeof(hkey), key, keylen, NULL, 0)) < 0)
      return (-1);

    key    = hkey;
    keylen = (size_t)hashlen;
  }

  // HMAC = H(K' ^ opad, H(K' ^ ipad, data))
  // K'   = Klen > b ? H(K) : K, padded with 0's
  // opad = 0x5c, ipad = 0x36
  for (i = 0; i < b && i < keylen; i ++)
    buffer[i] = key[i] ^ 0x36;
  for (; i < b; i ++)
    buffer[i] = 0x36;

  if ((hashlen = hash_data(algorithm, hash, sizeof(hash), buffer, b, data, datalen)) < 0)
    return (-1);

  for (i = 0; i < b && i < keylen; i ++)
    buffer[i] = key[i] ^ 0x5c;
  for (; i < b; i ++)
    buffer[i] = 0x5c;

  return (hash_data(algorithm, hmac, hmacsize, buffer, b, hash, (size_t)hashlen));
}


//
// 'hash_data()' - Hash up to two blocks of data.
//

static ssize_t				// O - Size of hash or `-1` on error
hash_data(const char    *algorithm,	// I - Algorithm
          unsigned char *hash,		// I - Hash buffer
          size_t        hashsize,	// I - Size of hash buffer
          const void    *a,		// I - First block
          size_t        alen,		// I - Length of first block
          const void    *b,		// I - Second block or `NULL` for none
          size_t        blen)		// I - Length of second block or `0` for none
{
  unsigned	hashlen;		// Length of hash
  unsigned char	hashtemp[64];		// Temporary hash buffer
#ifdef HAVE_OPENSSL
  const EVP_MD	*md = NULL;		// Message digest implementation
  EVP_MD_CTX	*ctx;			// Context
#else // HAVE_GNUTLS
  gnutls_digest_algorithm_t alg = GNUTLS_DIG_UNKNOWN;
					// Algorithm
  gnutls_hash_hd_t ctx;			// Context
#endif // HAVE_OPENSSL


  if (!strcmp(algorithm, "md5"))
  {
    // Some versions of GNU TLS and OpenSSL disable MD5 without warning...
    _cups_md5_state_t	state;		// MD5 state info

    if (hashsize < 16)
      goto too_small;

    _cupsMD5Init(&state);
    _cupsMD5Append(&state, a, (int)alen);
    if (b && blen)
      _cupsMD5Append(&state, b, (int)blen);
    _cupsMD5Finish(&state, hash);

    return (16);
  }

#ifdef HAVE_OPENSSL
  if (!strcmp(algorithm, "sha"))
  {
    // SHA-1
    md = EVP_sha1();
  }
  else if (!strcmp(algorithm, "sha2-224"))
  {
    md = EVP_sha224();
  }
  else if (!strcmp(algorithm, "sha2-256"))
  {
    md = EVP_sha256();
  }
  else if (!strcmp(algorithm, "sha2-384"))
  {
    md = EVP_sha384();
  }
  else if (!strcmp(algorithm, "sha2-512"))
  {
    md = EVP_sha512();
  }

  if (md)
  {
    ctx = EVP_MD_CTX_new();
    EVP_DigestInit(ctx, md);
    EVP_DigestUpdate(ctx, a, alen);
    if (b && blen)
      EVP_DigestUpdate(ctx, b, blen);
    EVP_DigestFinal(ctx, hashtemp, &hashlen);

    if (hashlen > hashsize)
      goto too_small;

    memcpy(hash, hashtemp, hashlen);

    return ((ssize_t)hashlen);
  }

#else // HAVE_GNUTLS
  if (!strcmp(algorithm, "sha"))
  {
    // SHA-1
    alg = GNUTLS_DIG_SHA1;
  }
  else if (!strcmp(algorithm, "sha2-224"))
  {
    alg = GNUTLS_DIG_SHA224;
  }
  else if (!strcmp(algorithm, "sha2-256"))
  {
    alg = GNUTLS_DIG_SHA256;
  }
  else if (!strcmp(algorithm, "sha2-384"))
  {
    alg = GNUTLS_DIG_SHA384;
  }
  else if (!strcmp(algorithm, "sha2-512"))
  {
    alg = GNUTLS_DIG_SHA512;
  }

  if (alg != GNUTLS_DIG_UNKNOWN)
  {
    hashlen = gnutls_hash_get_len(alg);

    if (hashlen > hashsize)
      goto too_small;

    gnutls_hash_init(&ctx, alg);
    gnutls_hash(ctx, a, alen);
    if (b && blen)
      gnutls_hash(ctx, b, blen);
    gnutls_hash_deinit(ctx, hashtemp);

    memcpy(hash, hashtemp, hashlen);

    return ((ssize_t)hashlen);
  }
#endif // HAVE_OPENSSL

  // Unknown hash algorithm...
  _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unknown hash algorithm."), 1);

  return (-1);

  // We get here if the buffer is too small.
  too_small:

  _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Hash buffer too small."), 1);
  return (-1);
}

