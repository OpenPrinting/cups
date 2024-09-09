//
// JSON Web Token API implementation for CUPS.
//
// Copyright © 2023-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"
#include "jwt.h"
#include "json-private.h"
#ifdef HAVE_OPENSSL
#  include <openssl/ecdsa.h>
#  include <openssl/evp.h>
#  include <openssl/rsa.h>
#else // HAVE_GNUTLS
#  include <gnutls/gnutls.h>
#  include <gnutls/abstract.h>
#  include <gnutls/crypto.h>
#endif // HAVE_OPENSSL


//
// Constants...
//

#define _CUPS_JWT_MAX_SIGNATURE	2048	// Enough for 512-bit signature


//
// Private types...
//

struct _cups_jwt_s			// JWT object
{
  cups_json_t	*jose;			// JOSE object
  char		*jose_string;		// JOSE string
  cups_json_t	*claims;		// JWT claims object
  char		*claims_string;		// JWT claims string
  cups_jwa_t	sigalg;			// Signature algorithm
  char		*sigkid;		// Key ID, if any
  size_t	sigsize;		// Size of signature
  unsigned char	*signature;		// Signature
};


//
// Local globals...
//

static const char * const cups_jwa_strings[CUPS_JWA_MAX] =
{
  "none",				// No algorithm
  "HS256",				// HMAC using SHA-256
  "HS384",				// HMAC using SHA-384
  "HS512",				// HMAC using SHA-512
  "RS256",				// RSASSA-PKCS1-v1_5 using SHA-256
  "RS384",				// RSASSA-PKCS1-v1_5 using SHA-384
  "RS512",				// RSASSA-PKCS1-v1_5 using SHA-512
  "ES256",				// ECDSA using P-256 and SHA-256
  "ES384",				// ECDSA using P-384 and SHA-384
  "ES512"				// ECDSA using P-521 and SHA-512
};
static const char * const cups_jwa_algorithms[CUPS_JWA_MAX] =
{
  NULL,
  "sha2-256",
  "sha2-384",
  "sha2-512",
  "sha2-256",
  "sha2-384",
  "sha2-512",
  "sha2-256",
  "sha2-384",
  "sha2-512"
};


//
// Local functions...
//

static cups_json_t *find_key(cups_json_t *jwk, cups_jwa_t sigalg, const char *kid);
#ifdef HAVE_OPENSSL
static BIGNUM	*make_bignum(cups_json_t *jwk, const char *key);
static void	make_bnstring(const BIGNUM *bn, char *buffer, size_t bufsize);
static EC_KEY	*make_ec_key(cups_json_t *jwk, bool verify);
static RSA	*make_rsa(cups_json_t *jwk);
#else // HAVE_GNUTLS
static gnutls_datum_t *make_datum(cups_json_t *jwk, const char *key);
static void	make_datstring(gnutls_datum_t *d, char *buffer, size_t bufsize);
static gnutls_privkey_t make_private_key(cups_json_t *jwk);
static gnutls_pubkey_t make_public_key(cups_json_t *jwk);
#endif // HAVE_OPENSSL
static bool	make_signature(cups_jwt_t *jwt, cups_jwa_t alg, cups_json_t *jwk, unsigned char *signature, size_t *sigsize, const char **sigkid);
static char	*make_string(cups_jwt_t *jwt, bool with_signature);


//
// 'cupsJWTDelete()' - Free the memory used for a JSON Web Token.
//
// @since CUPS 2.5@
//

void
cupsJWTDelete(cups_jwt_t *jwt)		// I - JWT object
{
  if (jwt)
  {
    cupsJSONDelete(jwt->jose);
    free(jwt->jose_string);
    cupsJSONDelete(jwt->claims);
    free(jwt->claims_string);
    free(jwt->sigkid);
    free(jwt->signature);
    free(jwt);
  }
}


//
// 'cupsJWTExportString()' - Export a JWT with the JWS Compact or JWS JSON (Flattened) Serialization format.
//
// This function exports a JWT to a JWS Compact or JWS JSON Serialization
// string.  The JSON output is always the "flattened" format since the JWT
// only contains a single signature.
//
// The return value must be freed using the `free` function.
//
// @since CUPS 2.5@
//

char *					// O - JWT/JWS Serialization string
cupsJWTExportString(
    cups_jwt_t        *jwt,		// I - JWT object
    cups_jws_format_t format)		// I - JWS serialization format
{
  char	*ret = NULL;			// Return value


  if (jwt)
  {
    if (format == CUPS_JWS_FORMAT_COMPACT)
    {
      // Compact token string
      ret = make_string(jwt, true);
    }
    else
    {
      // JSON (flattened) serialized string
      cups_json_t *json;		// JSON serialization
      char	*payload,		// Payload value
		signature[((_CUPS_JWT_MAX_SIGNATURE + 2) * 4 / 3) + 1];
					// Base64URL-encoded signature value

      // The payload is the compact token string without signature...
      json = cupsJSONNew(NULL, NULL, CUPS_JTYPE_OBJECT);

      payload = make_string(jwt, false);
      cupsJSONNewString(json, cupsJSONNewKey(json, NULL, "payload"), payload);
      free(payload);

      if (jwt->sigsize)
      {
        if (jwt->sigkid)
        {
	  cups_json_t *header;		// Unprotected header

	  header = cupsJSONNew(json, cupsJSONNewKey(json, NULL, "header"), CUPS_JTYPE_OBJECT);
	  cupsJSONNewString(header, cupsJSONNewKey(header, NULL, "kid"), jwt->sigkid);
	}

        // Add the Base64URL-encoded signature value...
        httpEncode64_3(signature, sizeof(signature), (char *)jwt->signature, jwt->sigsize, true);
	cupsJSONNewString(json, cupsJSONNewKey(json, NULL, "signature"), signature);
      }

      ret = cupsJSONExportString(json);
      cupsJSONDelete(json);
    }
  }

  return (ret);
}


//
// 'cupsJWTGetAlgorithm()' - Get the signature algorithm used by a JSON Web Token.
//
// @since CUPS 2.5@
//

cups_jwa_t				// O - Signature algorithm
cupsJWTGetAlgorithm(cups_jwt_t *jwt)	// I - JWT object
{
  return (jwt ? jwt->sigalg : CUPS_JWA_NONE);
}


//
// 'cupsJWTGetClaimNumber()' - Get the number value of a claim.
//
// @since CUPS 2.5@
//

double					// O - Number value
cupsJWTGetClaimNumber(cups_jwt_t *jwt,	// I - JWT object
                      const char *claim)// I - Claim name
{
  cups_json_t	*node;			// Value node


  if (jwt && (node = cupsJSONFind(jwt->claims, claim)) != NULL)
    return (cupsJSONGetNumber(node));
  else
    return (0.0);
}


//
// 'cupsJWTGetClaimString()' - Get the string value of a claim.
//
// @since CUPS 2.5@
//

const char *				// O - String value
cupsJWTGetClaimString(cups_jwt_t *jwt,	// I - JWT object
                      const char *claim)// I - Claim name
{
  cups_json_t	*node;			// Value node


  if (jwt && (node = cupsJSONFind(jwt->claims, claim)) != NULL)
    return (cupsJSONGetString(node));
  else
    return (NULL);
}


//
// 'cupsJWTGetClaimType()' - Get the value type of a claim.
//
// @since CUPS 2.5@
//

cups_jtype_t				// O - JSON value type
cupsJWTGetClaimType(cups_jwt_t *jwt,	// I - JWT object
                    const char *claim)	// I - Claim name
{
  cups_json_t	*node;			// Value node


  if (jwt && (node = cupsJSONFind(jwt->claims, claim)) != NULL)
    return (cupsJSONGetType(node));
  else
    return (CUPS_JTYPE_NULL);
}


//
// 'cupsJWTGetClaimValue()' - Get the value node of a claim.
//
// @since CUPS 2.5@
//

cups_json_t *				// O - JSON value node
cupsJWTGetClaimValue(cups_jwt_t *jwt,	// I - JWT object
                     const char *claim)	// I - Claim name
{
  if (jwt)
    return (cupsJSONFind(jwt->claims, claim));
  else
    return (NULL);
}


//
// 'cupsJWTGetClaims()' - Get the JWT claims as a JSON object.
//
// @since CUPS 2.5@
//

cups_json_t *				// O - JSON object
cupsJWTGetClaims(cups_jwt_t *jwt)	// I - JWT object
{
  return (jwt ? jwt->claims : NULL);
}


//
// 'cupsJWTGetHeaderNumber()' - Get the number value of a protected header.
//
// @since CUPS 2.5@
//

double					// O - Number value
cupsJWTGetHeaderNumber(
    cups_jwt_t *jwt,			// I - JWT object
    const char *header)			// I - Header name
{
  cups_json_t	*node;			// Value node


  if (jwt && (node = cupsJSONFind(jwt->jose, header)) != NULL)
    return (cupsJSONGetNumber(node));
  else
    return (0.0);
}


//
// 'cupsJWTGetHeaderString()' - Get the string value of a protected header.
//
// @since CUPS 2.5@
//

const char *				// O - String value
cupsJWTGetHeaderString(
    cups_jwt_t *jwt,			// I - JWT object
    const char *header)			// I - Header name
{
  cups_json_t	*node;			// Value node


  if (jwt && (node = cupsJSONFind(jwt->jose, header)) != NULL)
    return (cupsJSONGetString(node));
  else
    return (NULL);
}


//
// 'cupsJWTGetHeaderType()' - Get the value type of a protected header.
//
// @since CUPS 2.5@
//

cups_jtype_t				// O - JSON value type
cupsJWTGetHeaderType(
    cups_jwt_t *jwt,			// I - JWT object
    const char *header)			// I - Header name
{
  cups_json_t	*node;			// Value node


  if (jwt && (node = cupsJSONFind(jwt->jose, header)) != NULL)
    return (cupsJSONGetType(node));
  else
    return (CUPS_JTYPE_NULL);
}


//
// 'cupsJWTGetHeaderValue()' - Get the value node of a protected header.
//
// @since CUPS 2.5@
//

cups_json_t *				// O - JSON value node
cupsJWTGetHeaderValue(
    cups_jwt_t *jwt,			// I - JWT object
    const char *header)			// I - Header name
{
  if (jwt)
    return (cupsJSONFind(jwt->jose, header));
  else
    return (NULL);
}


//
// 'cupsJWTGetHeaders()' - Get the JWT protected headers as a JSON object.
//
// @since CUPS 2.5@
//

cups_json_t *				// O - JSON object
cupsJWTGetHeaders(cups_jwt_t *jwt)	// I - JWT object
{
  return (jwt ? jwt->jose : NULL);
}


//
// 'cupsJWTHasValidSignature()' - Determine whether the JWT has a valid signature.
//
// @since CUPS 2.5@
//

bool					// O - `true` if value, `false` otherwise
cupsJWTHasValidSignature(
    cups_jwt_t  *jwt,			// I - JWT object
    cups_json_t *jwk)			// I - JWK key set
{
  bool		ret = false;		// Return value
  unsigned char	signature[_CUPS_JWT_MAX_SIGNATURE];
					// Signature
  const char	*sigkid;		// Key ID, if any
  size_t	sigsize = _CUPS_JWT_MAX_SIGNATURE;
					// Size of signature
  char		*text;			// Signature text
  size_t	text_len;		// Length of signature text
#ifdef HAVE_OPENSSL
  unsigned char	hash[128];		// Hash
  ssize_t	hash_len;		// Length of hash
  RSA		*rsa;			// RSA public key
  EC_KEY	*ec;			// ECDSA public key
  static int	nids[] = { NID_sha256, NID_sha384, NID_sha512 };
					// Hash NIDs
#else // HAVE_GNUTLS
  gnutls_pubkey_t	key;		// Public key
  gnutls_datum_t	text_datum,	// Text datum
			sig_datum;	// Signature datum
  static int algs[] = { GNUTLS_DIG_SHA256, GNUTLS_DIG_SHA384, GNUTLS_DIG_SHA512, GNUTLS_SIGN_ECDSA_SHA256, GNUTLS_SIGN_ECDSA_SHA384, GNUTLS_SIGN_ECDSA_SHA512 };
					// Hash algorithms
#endif // HAVE_OPENSSL


  // Range check input...
  if (!jwt || !jwt->signature || !jwk)
    return (false);

  DEBUG_printf("1cupsJWTHasValidSignature: sigalg=%d, orig sig[%u]=<%02X%02X%02X%02X...%02X%02X%02X%02X>", jwt->sigalg, (unsigned)jwt->sigsize, jwt->signature[0], jwt->signature[1], jwt->signature[2], jwt->signature[3], jwt->signature[jwt->sigsize - 4], jwt->signature[jwt->sigsize - 3], jwt->signature[jwt->sigsize - 2], jwt->signature[jwt->sigsize - 1]);

  switch (jwt->sigalg)
  {
    case CUPS_JWA_HS256 :
    case CUPS_JWA_HS384 :
    case CUPS_JWA_HS512 :
	// Calculate signature with keys...
	sigkid = jwt->sigkid;
	if (!make_signature(jwt, jwt->sigalg, jwk, signature, &sigsize, &sigkid))
	  break;

	DEBUG_printf("1cupsJWTHasValidSignature: calc sig(%u) = %02X%02X%02X%02X...%02X%02X%02X%02X", (unsigned)sigsize, signature[0], signature[1], signature[2], signature[3], signature[sigsize - 4], signature[sigsize - 3], signature[sigsize - 2], signature[sigsize - 1]);

	// Compare and return the result...
	ret = jwt->sigsize == sigsize && !memcmp(jwt->signature, signature, sigsize);
	break;

    case CUPS_JWA_RS256 :
    case CUPS_JWA_RS384 :
    case CUPS_JWA_RS512 :
	// Get the message hash...
        text     = make_string(jwt, false);
        text_len = strlen(text);
        jwk      = find_key(jwk, jwt->sigalg, jwt->sigkid);

#ifdef HAVE_OPENSSL
        hash_len = cupsHashData(cups_jwa_algorithms[jwt->sigalg], text, text_len, hash, sizeof(hash));

        if ((rsa = make_rsa(jwk)) != NULL)
        {
	  ret = RSA_verify(nids[jwt->sigalg - CUPS_JWA_RS256], hash, hash_len, jwt->signature, jwt->sigsize, rsa) == 1;

	  RSA_free(rsa);
        }

#else // HAVE_GNUTLS
        if ((key = make_public_key(jwk)) != NULL)
        {
          text_datum.data = (unsigned char *)text;
          text_datum.size = (unsigned)text_len;

          sig_datum.data  = jwt->signature;
          sig_datum.size  = (unsigned)jwt->sigsize;

          ret = !gnutls_pubkey_verify_data2(key, algs[jwt->sigalg - CUPS_JWA_RS256], 0, &text_datum, &sig_datum);
          gnutls_pubkey_deinit(key);
        }
#endif // HAVE_OPENSSL

        // Free memory
	free(text);
        break;

    case CUPS_JWA_ES256 :
    case CUPS_JWA_ES384 :
    case CUPS_JWA_ES512 :
	// Get the message hash...
        text     = make_string(jwt, false);
        text_len = strlen(text);
        jwk      = find_key(jwk, jwt->sigalg, jwt->sigkid);

#ifdef HAVE_OPENSSL
        hash_len = cupsHashData(cups_jwa_algorithms[jwt->sigalg], text, text_len, hash, sizeof(hash));

        if ((ec = make_ec_key(jwk, true)) != NULL)
        {
          // Convert binary signature into ECDSA signature for OpenSSL
          ECDSA_SIG	*ec_sig;	// EC signature
	  BIGNUM	*r, *s;		// Signature coordinates
	  int		sig_len;	// Size of coordinates

	  ec_sig = ECDSA_SIG_new();
	  sig_len = (int)jwt->sigsize / 2;
	  r       = BN_new();
	  s       = BN_new();
	  BN_bin2bn(jwt->signature, sig_len, r);
	  BN_bin2bn(jwt->signature + sig_len, sig_len, s);
	  ECDSA_SIG_set0(ec_sig, r, s);

          // Verify signature and clean up...
	  ret = ECDSA_do_verify(hash, hash_len, ec_sig, ec) == 1;

          ECDSA_SIG_free(ec_sig);
	  EC_KEY_free(ec);
        }

#else // HAVE_GNUTLS
        if ((key = make_public_key(jwk)) != NULL)
        {
	  gnutls_datum_t r, s;		// Signature coordinates

          text_datum.data = (unsigned char *)text;
          text_datum.size = (unsigned)text_len;

          r.data = jwt->signature;
          r.size = (unsigned)jwt->sigsize / 2;
	  s.data = jwt->signature + jwt->sigsize / 2;
          s.size = (unsigned)jwt->sigsize / 2;

	  gnutls_encode_rs_value(&sig_datum, &r, &s);

          ret = !gnutls_pubkey_verify_data2(key, algs[jwt->sigalg - CUPS_JWA_RS256], 0, &text_datum, &sig_datum);
	  gnutls_free(sig_datum.data);
          gnutls_pubkey_deinit(key);
        }
#endif // HAVE_OPENSSL

        // Free memory
	free(text);
        break;

    default :
        DEBUG_printf("1cupsJWTHasValidSignature: Algorithm %d not supported.", jwt->sigalg);
	break;
  }

  return (ret);
}


//
// 'cupsJWTImportString()' - Import a JSON Web Token or JSON Web Signature.
//
// @since CUPS 2.5@
//

cups_jwt_t *				// O - JWT object
cupsJWTImportString(
    const char        *s,		// I - JWS string
    cups_jws_format_t format)		// I - JWS serialization format
{
  cups_jwt_t	*jwt;			// JWT object
  size_t	datalen;		// Size of data
  char		data[65536];		// Data
  const char	*kid,			// Key identifier
		*alg;			// Signature algorithm, if any


  // Allocate a JWT...
  if ((jwt = calloc(1, sizeof(cups_jwt_t))) == NULL)
    return (NULL);

  // Import it...
  if (format == CUPS_JWS_FORMAT_COMPACT)
  {
    // Import compact Base64URL-encoded token...
    const char	*tokptr;		// Pointer into the token

    // Extract the JOSE header...
    datalen = sizeof(data) - 1;
    if (!httpDecode64_3(data, &datalen, s, &tokptr) || !tokptr || *tokptr != '.')
      goto import_error;

    tokptr ++;
    data[datalen] = '\0';
    jwt->jose_string = strdup(data);
    if ((jwt->jose = cupsJSONImportString(data)) == NULL)
      goto import_error;

    // Extract the JWT claims...
    datalen = sizeof(data) - 1;
    if (!httpDecode64_3(data, &datalen, tokptr, &tokptr) || !tokptr || *tokptr != '.')
      goto import_error;

    tokptr ++;
    data[datalen] = '\0';
    jwt->claims_string = strdup(data);
    if ((jwt->claims = cupsJSONImportString(data)) == NULL)
      goto import_error;

    // Extract the signature, if any...
    datalen = sizeof(data);
    if (!httpDecode64_3(data, &datalen, tokptr, &tokptr) || !tokptr || *tokptr)
      goto import_error;

    if (datalen > 0)
    {
      if ((jwt->signature = malloc(datalen)) == NULL)
	goto import_error;

      memcpy(jwt->signature, data, datalen);
      jwt->sigsize = datalen;
    }
  }
  else
  {
    // Import JSON...
    cups_json_t	*json,			// JSON data
		*json_value,		// BASE64URL-encoded string value node
		*signatures,		// Signatures array
		*signature;		// Signature element to load
    const char	*value,			// C string value
		*valueptr;		// Pointer into value

    if ((json = cupsJSONImportString(s)) == NULL)
      goto import_error;

    // Copy the payload...
    if ((json_value = cupsJSONFind(json, "payload")) == NULL || (value = cupsJSONGetString(json_value)) == NULL)
    {
      cupsJSONDelete(json);
      goto import_error;
    }

    datalen = sizeof(data) - 1;
    if (!httpDecode64_3(data, &datalen, value, &valueptr) || !valueptr || *valueptr)
    {
      cupsJSONDelete(json);
      goto import_error;
    }

    data[datalen] = '\0';
    jwt->claims_string = strdup(data);
    if ((jwt->claims = cupsJSONImportString(data)) == NULL)
    {
      cupsJSONDelete(json);
      goto import_error;
    }

    // See if we have a flattened JSON JWT...
    if ((signatures = cupsJSONFind(json, "signatures")) != NULL)
    {
      // Use the first protected header and signature in the array...
      signature = cupsJSONGetChild(signatures, 0);
    }
    else
    {
      // Use the protected header and signature from the main JSON object...
      signature = json;
    }

    // Copy the protected header and signature values...
    if ((json_value = cupsJSONFind(signature, "protected")) == NULL || (value = cupsJSONGetString(json_value)) == NULL)
    {
      cupsJSONDelete(json);
      goto import_error;
    }

    datalen = sizeof(data) - 1;
    if (!httpDecode64_3(data, &datalen, value, &valueptr) || !valueptr || *valueptr)
    {
      cupsJSONDelete(json);
      goto import_error;
    }

    data[datalen] = '\0';
    jwt->jose_string = strdup(data);
    if ((jwt->jose = cupsJSONImportString(data)) == NULL)
    {
      cupsJSONDelete(json);
      goto import_error;
    }

    if ((json_value = cupsJSONFind(signature, "signature")) == NULL || (value = cupsJSONGetString(json_value)) == NULL)
    {
      cupsJSONDelete(json);
      goto import_error;
    }

    datalen = sizeof(data);
    if (!httpDecode64_3(data, &datalen, value, &valueptr) || !valueptr || *valueptr)
    {
      cupsJSONDelete(json);
      goto import_error;
    }

    if (datalen > 0)
    {
      if ((jwt->signature = malloc(datalen)) == NULL)
	goto import_error;

      memcpy(jwt->signature, data, datalen);
      jwt->sigsize = datalen;
    }
  }

#ifdef DEBUG
  if (jwt->sigsize >= 8)
    DEBUG_printf("1cupsJWTImportString: signature[%u]=<%02X%02X%02X%02X...%02X%02X%02X%02X>", (unsigned)jwt->sigsize, jwt->signature[0], jwt->signature[1], jwt->signature[2], jwt->signature[3], jwt->signature[jwt->sigsize - 4], jwt->signature[jwt->sigsize - 3], jwt->signature[jwt->sigsize - 2], jwt->signature[jwt->sigsize - 1]);
  else if (jwt->sigsize > 0)
    DEBUG_printf("1cupsJWTImportString: signature[%u]=<...>", (unsigned)jwt->sigsize);
#endif // DEBUG

  // Check the algorithm used in the protected header...
  if ((alg = cupsJSONGetString(cupsJSONFind(jwt->jose, "alg"))) != NULL)
  {
    cups_jwa_t	sigalg;			// Signing algorithm

    DEBUG_printf("1cupsJWTImportString: alg=\"%s\"", alg);

    for (sigalg = CUPS_JWA_NONE; sigalg < CUPS_JWA_MAX; sigalg ++)
    {
      if (!strcmp(alg, cups_jwa_strings[sigalg]))
      {
        jwt->sigalg = sigalg;
        DEBUG_printf("1cupsJWTImportString: sigalg=%d", sigalg);
        break;
      }
    }
  }

  if ((kid = cupsJSONGetString(cupsJSONFind(jwt->jose, "kid"))) != NULL)
  {
    DEBUG_printf("1cupsJWTImportString: kid=\"%s\"", kid);
    jwt->sigkid = strdup(kid);
  }

  // Can't have signature with none or no signature for !none...
  if ((jwt->sigalg == CUPS_JWA_NONE) != (jwt->sigsize == 0))
    goto import_error;

  // Return the new JWT...
  return (jwt);

  import_error:

  _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Invalid JSON web token."), 1);
  cupsJWTDelete(jwt);

  return (NULL);
}


//
// 'cupsJWTMakePrivateKey()' - Make a JSON Web Key for encryption and signing.
//
// This function makes a JSON Web Key (JWK) for the specified JWS/JWE algorithm
// for use when signing or encrypting JSON Web Tokens.  The resulting JWK
// *must not* be provided to clients - instead, call @link cupsJWTMakePublicKey@
// to produce a public key subset suitable for verification and decryption.
//
// @since CUPS 2.5@
//

cups_json_t *				// O - Private JSON Web Key or `NULL` on error
cupsJWTMakePrivateKey(cups_jwa_t alg)	// I - Signing/encryption algorithm
{
  cups_json_t	*jwk,			// Private JSON Web Key
		*node;			// Current node
  char		kid[256];		// Key identifier value
  const char	*kty;			// Key type


  if (alg < CUPS_JWA_HS256 || alg > CUPS_JWA_ES512)
    return (NULL);

  jwk = cupsJSONNew(NULL, NULL, CUPS_JTYPE_OBJECT);
  node = cupsJSONNewKey(jwk, NULL, "kty");

  if (alg >= CUPS_JWA_HS256 && alg <= CUPS_JWA_HS512)
  {
    // Simple N-byte random key...
    unsigned char	key[128];	// Key bytes
    size_t		key_len;	// Key length
    char		key_base64[172];// Base64URL-encoded key bytes

    node = cupsJSONNewString(jwk, node, kty = "oct");

    key_len = alg == CUPS_JWA_HS256 ? 64 : 128;
#ifdef HAVE_OPENSSL
    RAND_bytes(key, key_len);
#else // HAVE_GNUTLS
    gnutls_rnd(GNUTLS_RND_KEY, key, key_len);
#endif // HAVE_OPENSSL

    httpEncode64_3(key_base64, sizeof(key_base64), (char *)key, key_len, true);
    node = cupsJSONNewKey(jwk, node, "k");
    node = cupsJSONNewString(jwk, node, key_base64);
  }
  else if (alg >= CUPS_JWA_RS256 && alg <= CUPS_JWA_RS512)
  {
    // 3072-bit RSA key
    char	n[1024],		// Public key modulus
		e[1024],		// Public key exponent
		d[1024],		// Private key exponent
		p[1024],		// Private key first prime factor
		q[1024],		// Private key second prime factor
		dp[1024],		// First factor exponent
		dq[1024],		// Second factor exponent
		qi[1024];		// First CRT coefficient

#ifdef HAVE_OPENSSL
    RSA	*rsa;			// RSA public/private key

    rsa = RSA_generate_key(3072, 0x10001, NULL, NULL);
    make_bnstring(RSA_get0_n(rsa), n, sizeof(n));
    make_bnstring(RSA_get0_e(rsa), e, sizeof(e));
    make_bnstring(RSA_get0_d(rsa), d, sizeof(d));
    make_bnstring(RSA_get0_p(rsa), p, sizeof(p));
    make_bnstring(RSA_get0_q(rsa), q, sizeof(q));
    make_bnstring(RSA_get0_dmp1(rsa), dp, sizeof(dp));
    make_bnstring(RSA_get0_dmq1(rsa), dq, sizeof(dq));
    make_bnstring(RSA_get0_iqmp(rsa), qi, sizeof(qi));

    RSA_free(rsa);

#else // HAVE_GNUTLS
    gnutls_privkey_t	key;		// Private key
    gnutls_datum_t	dat_n, dat_e, dat_d, dat_p, dat_q, dat_dp, dat_dq, dat_qi;
					// RSA parameters

    gnutls_privkey_init(&key);
    gnutls_privkey_generate(key, GNUTLS_PK_RSA, 3072, 0);
    gnutls_privkey_export_rsa_raw(key, &dat_n, &dat_e, &dat_d, &dat_p, &dat_q, &dat_qi, &dat_dp, &dat_dq);
    make_datstring(&dat_n, n, sizeof(n));
    make_datstring(&dat_e, e, sizeof(e));
    make_datstring(&dat_d, d, sizeof(d));
    make_datstring(&dat_p, p, sizeof(p));
    make_datstring(&dat_q, q, sizeof(q));
    make_datstring(&dat_qi, qi, sizeof(qi));
    make_datstring(&dat_dp, dp, sizeof(dp));
    make_datstring(&dat_dq, dq, sizeof(dq));
    gnutls_privkey_deinit(key);
#endif // HAVE_OPENSSL

    node = cupsJSONNewString(jwk, node, kty = "RSA");
    node = cupsJSONNewKey(jwk, node, "n");
    node = cupsJSONNewString(jwk, node, n);
    node = cupsJSONNewKey(jwk, node, "e");
    node = cupsJSONNewString(jwk, node, e);
    node = cupsJSONNewKey(jwk, node, "d");
    node = cupsJSONNewString(jwk, node, d);
    node = cupsJSONNewKey(jwk, node, "p");
    node = cupsJSONNewString(jwk, node, p);
    node = cupsJSONNewKey(jwk, node, "q");
    node = cupsJSONNewString(jwk, node, q);
    node = cupsJSONNewKey(jwk, node, "dp");
    node = cupsJSONNewString(jwk, node, dp);
    node = cupsJSONNewKey(jwk, node, "dq");
    node = cupsJSONNewString(jwk, node, dq);
    node = cupsJSONNewKey(jwk, node, "qi");
    node = cupsJSONNewString(jwk, node, qi);
  }
  else
  {
    // N-bit ECC key
    char	x[1024],		// X coordinate
		y[1024],		// Y coordinate
		d[1024];		// Private key
    static const char * const curves[] =
    {
      "P-256",
      "P-384",
      "P-521"
    };

#ifdef HAVE_OPENSSL
    EC_KEY	*ec;			// EC object
    const EC_GROUP *group;		// Group
    const EC_POINT *pubkey;		// Public key portion
    BIGNUM	*bx, *by;		// Public key coordinates

    if (alg == CUPS_JWA_ES256)
      ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    else if (alg == CUPS_JWA_ES384)
      ec = EC_KEY_new_by_curve_name(NID_secp384r1);
    else
      ec = EC_KEY_new_by_curve_name(NID_secp521r1);

    EC_KEY_generate_key(ec);

    make_bnstring(EC_KEY_get0_private_key(ec), d, sizeof(d));

    group  = EC_KEY_get0_group(ec);
    pubkey = EC_KEY_get0_public_key(ec);

    bx = BN_new();
    by = BN_new();
    EC_POINT_get_affine_coordinates(group, pubkey, bx, by, NULL);
    make_bnstring(bx, x, sizeof(x));
    make_bnstring(by, y, sizeof(y));
    BN_free(bx);
    BN_free(by);

    EC_KEY_free(ec);

#else // HAVE_GNUTLS
    gnutls_privkey_t	key;		// Private key
    gnutls_ecc_curve_t	dat_curve;	// Curve
    gnutls_datum_t	dat_x, dat_y, dat_d;
					// ECDSA parameters

    gnutls_privkey_init(&key);
    gnutls_privkey_generate(key, GNUTLS_PK_EC, GNUTLS_CURVE_TO_BITS((GNUTLS_ECC_CURVE_SECP256R1 + (alg - CUPS_JWA_ES256))), 0);
    gnutls_privkey_export_ecc_raw(key, &dat_curve, &dat_x, &dat_y, &dat_d);
    make_datstring(&dat_x, x, sizeof(x));
    make_datstring(&dat_y, y, sizeof(y));
    make_datstring(&dat_d, d, sizeof(d));
    gnutls_privkey_deinit(key);
#endif // HAVE_OPENSSL

    node = cupsJSONNewString(jwk, node, kty = "EC");
    node = cupsJSONNewKey(jwk, node, "crv");
    node = cupsJSONNewString(jwk, node, curves[alg - CUPS_JWA_ES256]);
    node = cupsJSONNewKey(jwk, node, "x");
    node = cupsJSONNewString(jwk, node, x);
    node = cupsJSONNewKey(jwk, node, "y");
    node = cupsJSONNewString(jwk, node, y);
    node = cupsJSONNewKey(jwk, node, "d");
    node = cupsJSONNewString(jwk, node, d);
  }

  // Add key identifier using key type and current date/time...
  snprintf(kid, sizeof(kid), "%s%ld", kty, (long)time(NULL));
  node = cupsJSONNewKey(jwk, node, "kid");
  /*node =*/ cupsJSONNewString(jwk, node, kid);

  return (jwk);
}


//
// 'cupsJWTMakePublicKey()' - Make a JSON Web Key for decryption and verification.
//
// This function makes a public JSON Web Key (JWK) from the specified private
// JWK suitable for use when decrypting or verifying a JWE/JWS message.
//
// @since CUPS 2.5@
//

cups_json_t *				// O - Public JSON Web Key or `NULL` on error
cupsJWTMakePublicKey(cups_json_t *jwk)	// I - Private JSON Web Key
{
  cups_json_t	*pubjwt = NULL,		// Public JSON Web Key
		*node = NULL;		// Current node
  const char	*kid,			// Key ID
		*kty;			// Key type


  kid = cupsJSONGetString(cupsJSONFind(jwk, "kid"));

  if ((kty = cupsJSONGetString(cupsJSONFind(jwk, "kty"))) == NULL)
  {
    // No type so we can't load it...
    return (NULL);
  }
  else if (!strcmp(kty, "RSA"))
  {
    // RSA private key
    const char	*n = cupsJSONGetString(cupsJSONFind(jwk, "n"));
    const char	*e = cupsJSONGetString(cupsJSONFind(jwk, "e"));

    pubjwt = cupsJSONNew(NULL, NULL, CUPS_JTYPE_OBJECT);
    node   = cupsJSONNewKey(pubjwt, NULL, "kty");
    node   = cupsJSONNewString(pubjwt, node, "RSA");
    node   = cupsJSONNewKey(pubjwt, node, "n");
    node   = cupsJSONNewString(pubjwt, node, n);
    node   = cupsJSONNewKey(pubjwt, node, "e");
    node   = cupsJSONNewString(pubjwt, node, e);
  }
  else if (!strcmp(kty, "EC"))
  {
    // ECDSA private key
    const char	*crv = cupsJSONGetString(cupsJSONFind(jwk, "crv"));
    const char	*x = cupsJSONGetString(cupsJSONFind(jwk, "x"));
    const char	*y = cupsJSONGetString(cupsJSONFind(jwk, "y"));

    pubjwt = cupsJSONNew(NULL, NULL, CUPS_JTYPE_OBJECT);
    node   = cupsJSONNewKey(pubjwt, NULL, "kty");
    node   = cupsJSONNewString(pubjwt, node, "EC");
    node   = cupsJSONNewKey(pubjwt, node, "crv");
    node   = cupsJSONNewString(pubjwt, node, crv);
    node   = cupsJSONNewKey(pubjwt, node, "x");
    node   = cupsJSONNewString(pubjwt, node, x);
    node   = cupsJSONNewKey(pubjwt, node, "y");
    node   = cupsJSONNewString(pubjwt, node, y);
  }

  if (pubjwt && kid)
  {
    node =    cupsJSONNewKey(pubjwt, node, "kid");
    /*node=*/ cupsJSONNewString(pubjwt, node, kid);
  }

  return (pubjwt);
}


//
// 'cupsJWTNew()' - Create a new, empty JSON Web Token.
//
// @since CUPS 2.5@
//

cups_jwt_t *				// O - JWT object
cupsJWTNew(const char *type)		// I - JWT type or `NULL` for default ("JWT")
{
  cups_jwt_t	*jwt;			// JWT object


  if ((jwt = calloc(1, sizeof(cups_jwt_t))) != NULL)
  {
    if ((jwt->jose = cupsJSONNew(NULL, NULL, CUPS_JTYPE_OBJECT)) != NULL)
    {
      cupsJSONNewString(jwt->jose, cupsJSONNewKey(jwt->jose, NULL, "typ"), type ? type : "JWT");

      if ((jwt->claims = cupsJSONNew(NULL, NULL, CUPS_JTYPE_OBJECT)) != NULL)
        return (jwt);
    }
  }

  cupsJWTDelete(jwt);
  return (NULL);
}


//
// 'cupsJWTSetClaimNumber()' - Set a claim number.
//
// @since CUPS 2.5@
//

void
cupsJWTSetClaimNumber(cups_jwt_t *jwt,	// I - JWT object
                      const char *claim,// I - Claim name
                      double     value)	// I - Number value
{
  // Range check input
  if (!jwt || !claim)
    return;

  // Remove existing claim string, if any...
  free(jwt->claims_string);
  jwt->claims_string = NULL;

  // Remove existing claim, if any...
  _cupsJSONDelete(jwt->claims, claim);

  // Add claim...
  cupsJSONNewNumber(jwt->claims, cupsJSONNewKey(jwt->claims, NULL, claim), value);
}


//
// 'cupsJWTSetClaimString()' - Set a claim string.
//
// @since CUPS 2.5@
//

void
cupsJWTSetClaimString(cups_jwt_t *jwt,	// I - JWT object
                      const char *claim,// I - Claim name
                      const char *value)// I - String value
{
  // Range check input
  if (!jwt || !claim || !value)
    return;

  // Remove existing claim string, if any...
  free(jwt->claims_string);
  jwt->claims_string = NULL;

  // Remove existing claim, if any...
  _cupsJSONDelete(jwt->claims, claim);

  // Add claim...
  cupsJSONNewString(jwt->claims, cupsJSONNewKey(jwt->claims, NULL, claim), value);
}


//
// 'cupsJWTSetClaimValue()' - Set a claim value.
//
// @since CUPS 2.5@
//

void
cupsJWTSetClaimValue(
    cups_jwt_t  *jwt,			// I - JWT object
    const char  *claim,			// I - Claim name
    cups_json_t *value)			// I - JSON value node
{
  // Range check input
  if (!jwt || !claim)
    return;

  // Remove existing claim string, if any...
  free(jwt->claims_string);
  jwt->claims_string = NULL;

  // Remove existing claim, if any...
  _cupsJSONDelete(jwt->claims, claim);

  // Add claim...
  cupsJSONAdd(jwt->claims, cupsJSONNewKey(jwt->claims, NULL, claim), value);
}


//
// 'cupsJWTSetHeaderNumber()' - Set a protected header number.
//
// @since CUPS 2.5@
//

void
cupsJWTSetHeaderNumber(
    cups_jwt_t *jwt,			// I - JWT object
    const char *header,			// I - Header name
    double     value)			// I - Number value
{
  // Range check input
  if (!jwt || !header)
    return;

  // Remove existing claim string, if any...
  free(jwt->claims_string);
  jwt->claims_string = NULL;

  // Remove existing claim, if any...
  _cupsJSONDelete(jwt->jose, header);

  // Add claim...
  cupsJSONNewNumber(jwt->jose, cupsJSONNewKey(jwt->jose, NULL, header), value);
}


//
// 'cupsJWTSetHeaderString()' - Set a protected header string.
//
// @since CUPS 2.5@
//

void
cupsJWTSetHeaderString(
    cups_jwt_t *jwt,			// I - JWT object
    const char *header,			// I - Header name
    const char *value)			// I - String value
{
  // Range check input
  if (!jwt || !header || !value)
    return;

  // Remove existing claim string, if any...
  free(jwt->claims_string);
  jwt->claims_string = NULL;

  // Remove existing claim, if any...
  _cupsJSONDelete(jwt->jose, header);

  // Add claim...
  cupsJSONNewString(jwt->jose, cupsJSONNewKey(jwt->jose, NULL, header), value);
}


//
// 'cupsJWTSetHeaderValue()' - Set a protected header value.
//
// @since CUPS 2.5@
//

void
cupsJWTSetHeaderValue(
    cups_jwt_t  *jwt,			// I - JWT object
    const char  *header,		// I - Header name
    cups_json_t *value)			// I - JSON value node
{
  // Range check input
  if (!jwt || !header)
    return;

  // Remove existing claim string, if any...
  free(jwt->claims_string);
  jwt->claims_string = NULL;

  // Remove existing claim, if any...
  _cupsJSONDelete(jwt->jose, header);

  // Add claim...
  cupsJSONAdd(jwt->jose, cupsJSONNewKey(jwt->jose, NULL, header), value);
}


//
// 'cupsJWTSign()' - Sign a JSON Web Token, creating a JSON Web Signature.
//
// @since CUPS 2.5@
//

bool					// O - `true` on success, `false` on error
cupsJWTSign(cups_jwt_t  *jwt,		// I - JWT object
            cups_jwa_t  alg,		// I - Signing algorithm
            cups_json_t *jwk)		// I - JWK key set
{
  unsigned char	signature[_CUPS_JWT_MAX_SIGNATURE];
					// Signature
  size_t	sigsize = _CUPS_JWT_MAX_SIGNATURE;
					// Size of signature
  const char	*sigkid = NULL;		// Key ID, if any


  // Range check input...
  if (!jwt || alg <= CUPS_JWA_NONE || alg >= CUPS_JWA_MAX || !jwk)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  // Remove existing JOSE string, if any...
  free(jwt->jose_string);
  _cupsJSONDelete(jwt->jose, "alg");
  cupsJSONNewString(jwt->jose, cupsJSONNewKey(jwt->jose, NULL, "alg"), cups_jwa_strings[alg]);

  jwt->jose_string = cupsJSONExportString(jwt->jose);

  // Clear existing signature...
  free(jwt->signature);
  free(jwt->sigkid);
  jwt->signature = NULL;
  jwt->sigkid    = NULL;
  jwt->sigsize   = 0;
  jwt->sigalg    = CUPS_JWA_NONE;

  // Create new signature...
  if (!make_signature(jwt, alg, jwk, signature, &sigsize, &sigkid))
  {
    DEBUG_puts("2cupsJWTSign: Unable to create signature.");
    return (false);
  }

  if (sigkid)
    jwt->sigkid = strdup(sigkid);

  if ((jwt->signature = malloc(sigsize)) == NULL)
  {
    DEBUG_printf("2cupsJWTSign: Unable to allocate %d bytes for signature.", (int)sigsize);
    return (false);
  }

  memcpy(jwt->signature, signature, sigsize);
  jwt->sigalg  = alg;
  jwt->sigsize = sigsize;

  return (true);
}


//
// 'find_key()' - Find the key by name or algorithm.
//

static cups_json_t *			// O - Key data
find_key(cups_json_t *jwk,		// I - Key set
         cups_jwa_t  alg,		// I - Signature algorithm
         const char  *kid)		// I - Signature key ID
{
  cups_json_t	*keys;			// Array of keys


  if ((keys = cupsJSONFind(jwk, "keys")) != NULL)
  {
    // Full key set, find the key we need to use...
    size_t	i,			// Looping var
		count;			// Number of keys
    cups_json_t	*current;		// Current key
    const char	*curkid,		// Current key ID
		*curkty;		// Current key type

    count = cupsJSONGetCount(keys);

    if (kid)
    {
      // Find the matching key ID
      for (i = 0; i < count; i ++)
      {
        current = cupsJSONGetChild(keys, i);
        curkid  = cupsJSONGetString(cupsJSONFind(current, "kid"));

        if (curkid && !strcmp(curkid, kid))
        {
          DEBUG_printf("4make_signature: Found matching key \"%s\" at %p.", curkid, (void *)current);
          jwk = current;
          break;
	}
      }
    }
    else
    {
      // Find a key that can be used for the specified algorithm
      for (i = 0; i < count; i ++)
      {
        current = cupsJSONGetChild(keys, i);
        curkty  = cupsJSONGetString(cupsJSONFind(current, "kty"));

        if (((!curkty || !strcmp(curkty, "ocy")) && alg >= CUPS_JWA_HS256 && alg <= CUPS_JWA_HS512) || (curkty && !strcmp(curkty, "RSA") && alg >= CUPS_JWA_RS256 && alg <= CUPS_JWA_RS512) || (curkty && !strcmp(curkty, "EC") && alg >= CUPS_JWA_ES256 && alg <= CUPS_JWA_ES512))
        {
          DEBUG_printf("4make_signature: Found compatible key \"%s\" at %p.", cupsJSONGetString(cupsJSONFind(current, "kid")), (void *)current);
          jwk = current;
          break;
	}
      }
    }
  }

  return (jwk);
}


#ifdef HAVE_OPENSSL
//
// 'make_bignum()' - Make a BIGNUM for the specified key.
//

static BIGNUM *				// O - BIGNUM object or `NULL` on error
make_bignum(cups_json_t *jwk,		// I - JSON web key
            const char  *key)		// I - Object key
{
  const char	*value,			// Key value
		*value_end;		// End of value
  unsigned char	value_bytes[1024];	// Decoded value
  size_t	value_len;		// Length of value


  // See if we have the value...
  if ((value = cupsJSONGetString(cupsJSONFind(jwk, key))) == NULL)
    return (NULL);

  // Decode and validate...
  value_len = sizeof(value_bytes);
  if (!httpDecode64_3((char *)value_bytes, &value_len, value, &value_end) || (value_end && *value_end))
    return (NULL);

  // Convert to a BIGNUM...
  return (BN_bin2bn(value_bytes, value_len, NULL));
}


//
// 'make_bnstring()' - Make a Base64URL-encoded string for a BIGNUM.
//

static void
make_bnstring(const BIGNUM *bn,		// I - Number
              char         *buffer,	// I - String buffer
              size_t       bufsize)	// I - Size of string buffer
{
  unsigned char	value_bytes[512];	// Value bytes
  size_t	value_len;		// Number of bytes


  if ((value_len = (size_t)BN_num_bytes(bn)) > sizeof(value_bytes))
  {
    *buffer = '\0';
    return;
  }

  BN_bn2bin(bn, value_bytes);
  httpEncode64_3(buffer, bufsize, (char *)value_bytes, value_len, true);
}


//
// 'make_ec_key()' - Make an ECDSA signing/verification object.
//

static EC_KEY *				// O - EC object or `NULL` on error
make_ec_key(cups_json_t *jwk,		// I - JSON web key
            bool        verify)		// I - `true` for verification only, `false` for signing/verification
{
  EC_KEY	*ec = NULL;		// EC object
  EC_GROUP	*group;			// Group parameters
  EC_POINT	*point;			// Public key point
  const char	*crv;			// EC curve ("P-256", "P-384", or "P-521")
  BIGNUM	*x,			// X coordinate
		*y,			// Y coordinate
		*d;			// Private key


  crv = cupsJSONGetString(cupsJSONFind(jwk, "crv"));
  x   = make_bignum(jwk, "x");
  y   = make_bignum(jwk, "y");
  d   = verify ? NULL : make_bignum(jwk, "d");

  if (!crv || ((!x || !y) && !d))
    goto ec_done;

  if (!strcmp(crv, "P-256"))
    ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  else if (!strcmp(crv, "P-384"))
    ec = EC_KEY_new_by_curve_name(NID_secp384r1);
  else if (!strcmp(crv, "P-521"))
    ec = EC_KEY_new_by_curve_name(NID_secp521r1);
  else
    goto ec_done;

  group = (EC_GROUP *)EC_KEY_get0_group(ec);
  point = EC_POINT_new(group);

  if (d)
  {
    // Set private key...
    EC_KEY_set_private_key(ec, d);

    // Create a new public key
    EC_POINT_mul(group, point, d, NULL, NULL, NULL);
  }
  else
  {
    // Create a public key using the supplied coordinates...
    EC_POINT_set_affine_coordinates_GFp(group, point, x, y, NULL);
  }

  // Set public key...
  EC_KEY_set_public_key(ec, point);

  ec_done:

  if (!ec)
  {
    BN_free(x);
    BN_free(y);
    BN_free(d);
  }

  return (ec);
}


//
// 'make_rsa()' - Create an RSA signing/verification object.
//

static RSA *				// O - RSA object or `NULL` on error
make_rsa(cups_json_t *jwk)		// I - JSON web key
{
  RSA		*rsa = NULL;		// RSA object
  BIGNUM	*n,			// Public key modulus
		*e,			// Public key exponent
		*d,			// Private key exponent
		*p,			// Private key first prime factor
		*q,			// Private key second prime factor
		*dp,			// First factor exponent
		*dq,			// Second factor exponent
		*qi;			// First CRT coefficient


  n  = make_bignum(jwk, "n");
  e  = make_bignum(jwk, "e");
  d  = make_bignum(jwk, "d");
  p  = make_bignum(jwk, "p");
  q  = make_bignum(jwk, "q");
  dp = make_bignum(jwk, "dp");
  dq = make_bignum(jwk, "dq");
  qi = make_bignum(jwk, "qi");

  if (!n || !e)
    goto rsa_done;

  rsa = RSA_new();
  RSA_set0_key(rsa, n, e, d);
  if (p && q)
    RSA_set0_factors(rsa, p, q);
  if (dp && dq && qi)
    RSA_set0_crt_params(rsa, dp, dq, qi);

  rsa_done:

  if (!rsa)
  {
    BN_free(n);
    BN_free(e);
    BN_free(d);
    BN_free(p);
    BN_free(q);
    BN_free(dp);
    BN_free(dq);
    BN_free(qi);
  }

  return (rsa);
}


#else // HAVE_GNUTLS
//
// 'make_datum()' - Make a datum value for a parameter.
//

static gnutls_datum_t *			// O - Datum or `NULL`
make_datum(cups_json_t *jwk,		// I - JSON web key
           const char  *key)		// I - Object key
{
  const char		*value,		// Key value
			*value_end;	// End of value
  unsigned char		value_bytes[1024];
					// Decoded value
  size_t		value_len;	// Length of value
  gnutls_datum_t	*datum;		// GNU TLS datum


  // See if we have the value...
  if ((value = cupsJSONGetString(cupsJSONFind(jwk, key))) == NULL)
    return (NULL);

  // Decode and validate...
  value_len = sizeof(value_bytes);
  if (!httpDecode64_3((char *)value_bytes, &value_len, value, &value_end) || (value_end && *value_end))
    return (NULL);

  // Convert to a datum...
  if ((datum = (gnutls_datum_t *)calloc(1, sizeof(gnutls_datum_t) + value_len)) != NULL)
  {
    // Set pointer and length, and copy value bytes...
    datum->data = (unsigned char *)(datum + 1);
    datum->size = (unsigned)value_len;

    memcpy(datum + 1, value_bytes, value_len);
  }

  return (datum);
}


//
// 'make_datstring()' - Make a Base64URL-encoded string from a datum.
//

static void
make_datstring(gnutls_datum_t *d,	// I - Datum
               char           *buffer,	// I - String buffer
               size_t         bufsize)	// I - Size of string buffer
{
  httpEncode64_3(buffer, bufsize, (char *)d->data, d->size, true);
  gnutls_free(d->data);
}


//
// 'make_private_key()' - Make a private key for EC or RSA signing.
//

static gnutls_privkey_t			// O - Private key or `NULL`
make_private_key(cups_json_t *jwk)	// I - JSON web key
{
  const char		*kty;		// Key type
  gnutls_privkey_t	key = NULL;	// Private key


  if ((kty = cupsJSONGetString(cupsJSONFind(jwk, "kty"))) == NULL)
  {
    // No type so we can't load it...
    return (NULL);
  }
  else if (!strcmp(kty, "RSA"))
  {
    // Get RSA parameters...
    gnutls_datum_t	*n,		// Public key modulus
			*e,		// Public key exponent
			*d,		// Private key exponent
			*p,		// Private key first prime factor
			*q,		// Private key second prime factor
			*dp,		// First factor exponent
			*dq,		// Second factor exponent
			*qi;		// First CRT coefficient

    n  = make_datum(jwk, "n");
    e  = make_datum(jwk, "e");
    d  = make_datum(jwk, "d");
    p  = make_datum(jwk, "p");
    q  = make_datum(jwk, "q");
    dp = make_datum(jwk, "dp");
    dq = make_datum(jwk, "dq");
    qi = make_datum(jwk, "qi");

    if (n && e && d && p && q && !gnutls_privkey_init(&key))
    {
      // Import RSA private key...
      if (gnutls_privkey_import_rsa_raw(key, n, e, d, p, q, qi, dp, dq))
      {
	gnutls_privkey_deinit(key);
	key = NULL;
      }
    }

    // Free memory...
    free(n);
    free(e);
    free(d);
    free(p);
    free(q);
    free(dp);
    free(dq);
    free(qi);
  }
  else if (!strcmp(kty, "EC"))
  {
    // Get EC parameters...
    const char		*crv;		// EC curve ("P-256", "P-384", or "P-521")
    gnutls_ecc_curve_t	curve;		// Curve constant
    gnutls_datum	*x,		// X coordinate
			*y,		// Y coordinate
			*d;		// Private key

    crv = cupsJSONGetString(cupsJSONFind(jwk, "crv"));

    if (!crv)
      return (NULL);
    else if (!strcmp(crv, "P-256"))
      curve = GNUTLS_ECC_CURVE_SECP256R1;
    else if (!strcmp(crv, "P-384"))
      curve = GNUTLS_ECC_CURVE_SECP384R1;
    else if (!strcmp(crv, "P-521"))
      curve = GNUTLS_ECC_CURVE_SECP521R1;
    else
      return (NULL);

    x = make_datum(jwk, "x");
    y = make_datum(jwk, "y");
    d = make_datum(jwk, "d");

    if (x && y && d && !gnutls_privkey_init(&key))
    {
      // Import EC private key...
      if (gnutls_privkey_import_ecc_raw(key, curve, x, y, d))
      {
	gnutls_privkey_deinit(key);
	key = NULL;
      }
    }

    // Free memory...
    free(x);
    free(y);
    free(d);
  }

  // Return whatever key we got...
  return (key);
}


//
// 'make_public_key()' - Make a public key for EC or RSA verification.
//

static gnutls_pubkey_t			// O - Public key or `NULL`
make_public_key(cups_json_t *jwk)	// I - JSON web key
{
  const char		*kty;		// Key type
  gnutls_pubkey_t	key = NULL;	// Private key


  if ((kty = cupsJSONGetString(cupsJSONFind(jwk, "kty"))) == NULL)
  {
    // No type so we can't load it...
    return (NULL);
  }
  else if (!strcmp(kty, "RSA"))
  {
    // Get RSA parameters...
    gnutls_datum_t	*n,		// Public key modulus
			*e;		// Public key exponent


    n  = make_datum(jwk, "n");
    e  = make_datum(jwk, "e");

    if (n && e && !gnutls_pubkey_init(&key))
    {
      // Import RSA private key...
      if (gnutls_pubkey_import_rsa_raw(key, n, e))
      {
	gnutls_pubkey_deinit(key);
	key = NULL;
      }
    }

    // Free memory and return...
    free(n);
    free(e);
  }
  else if (!strcmp(kty, "EC"))
  {
    // Get EC parameters...
    const char		*crv;		// EC curve ("P-256", "P-384", or "P-521")
    gnutls_ecc_curve_t	curve;		// Curve constant
    gnutls_datum	*x,		// X coordinate
			*y;		// Y coordinate

    crv = cupsJSONGetString(cupsJSONFind(jwk, "crv"));

    if (!crv)
      return (NULL);
    else if (!strcmp(crv, "P-256"))
      curve = GNUTLS_ECC_CURVE_SECP256R1;
    else if (!strcmp(crv, "P-384"))
      curve = GNUTLS_ECC_CURVE_SECP384R1;
    else if (!strcmp(crv, "P-521"))
      curve = GNUTLS_ECC_CURVE_SECP521R1;
    else
      return (NULL);

    x = make_datum(jwk, "x");
    y = make_datum(jwk, "y");

    if (x && y && !gnutls_pubkey_init(&key))
    {
      // Import EC public key...
      if (gnutls_pubkey_import_ecc_raw(key, curve, x, y))
      {
	gnutls_pubkey_deinit(key);
	key = NULL;
      }
    }

    // Free memory...
    free(x);
    free(y);
  }

  return (key);
}
#endif // HAVE_OPENSSL


//
// 'make_signature()' - Make a signature.
//

static bool				// O  - `true` on success, `false` on failure
make_signature(cups_jwt_t    *jwt,	// I  - JWT
               cups_jwa_t    alg,	// I  - Algorithm
               cups_json_t   *jwk,	// I  - JSON Web Key Set
               unsigned char *signature,// I  - Signature buffer
               size_t        *sigsize,	// IO - Signature size
               const char    **sigkid)	// IO - Key ID string, if any
{
  bool			ret = false;	// Return value
  char			*text;		// JWS Signing Input
  size_t		text_len;	// Length of signing input
#ifdef HAVE_OPENSSL
  static int		nids[] = { NID_sha256, NID_sha384, NID_sha512 };
					// Hash NIDs
#else // HAVE_GNUTLS
  gnutls_privkey_t	key;		// Private key
  gnutls_datum_t	text_datum,	// Text datum
			sig_datum;	// Signature datum
  static int algs[] = { GNUTLS_DIG_SHA256, GNUTLS_DIG_SHA384, GNUTLS_DIG_SHA512, GNUTLS_DIG_SHA256, GNUTLS_DIG_SHA384, GNUTLS_DIG_SHA512 };
					// Hash algorithms
#endif // HAVE_OPENSSL


  DEBUG_printf("3make_signature(jwt=%p, alg=%d, jwk=%p, signature=%p, sigsize=%p(%u), sigkid=%p(%s))", (void *)jwt, alg, (void *)jwk, (void *)signature, (void *)sigsize, (unsigned)*sigsize, (void *)sigkid, *sigkid);

  // Get text to sign...
  text     = make_string(jwt, false);
  text_len = strlen(text);
  jwk      = find_key(jwk, alg, *sigkid);

  if (alg >= CUPS_JWA_HS256 && alg <= CUPS_JWA_HS512)
  {
    // SHA-256/384/512 HMAC
    const char		*k;		// "k" value
    unsigned char	key[256];	// Key value
    size_t		key_len;	// Length of key
    ssize_t		hmac_len;	// Length of HMAC

    DEBUG_puts("4make_signature: HMAC signature");

    // Get key...
    memset(key, 0, sizeof(key));
    k       = cupsJSONGetString(cupsJSONFind(jwk, "k"));
    key_len = sizeof(key);
    if (!httpDecode64_3((char *)key, &key_len, k, NULL))
      goto done;

    if ((hmac_len = cupsHMACData(cups_jwa_algorithms[alg], key, key_len, text, text_len, signature, _CUPS_JWT_MAX_SIGNATURE)) < 0)
      goto done;

    *sigsize = (size_t)hmac_len;
    ret      = true;
  }
  else if (alg >= CUPS_JWA_RS256 && alg <= CUPS_JWA_RS512)
  {
    // RSASSA-PKCS1-v1_5 SHA-256/384/512
    DEBUG_puts("4make_signature: RSA signature");

#ifdef HAVE_OPENSSL
    unsigned char hash[128];		// SHA-256/384/512 hash
    ssize_t	hash_len;		// Length of hash
    unsigned	siglen = (unsigned)*sigsize;
					// Length of signature
    RSA		*rsa;			// RSA public/private key

    if ((rsa = make_rsa(jwk)) != NULL)
    {
      hash_len = cupsHashData(cups_jwa_algorithms[alg], text, text_len, hash, sizeof(hash));
      if (RSA_sign(nids[alg - CUPS_JWA_RS256], hash, hash_len, signature, &siglen, rsa) == 1)
      {
        *sigsize = siglen;
        ret      = true;
      }

      RSA_free(rsa);
    }
#else // HAVE_GNUTLS
    if ((key = make_private_key(jwk)) != NULL)
    {
      text_datum.data = (unsigned char *)text;
      text_datum.size = (unsigned)text_len;
      sig_datum.data  = NULL;
      sig_datum.size  = 0;

      if (!gnutls_privkey_sign_data(key, algs[alg - CUPS_JWA_RS256], 0, &text_datum, &sig_datum) && sig_datum.size <= *sigsize)
      {
        memcpy(signature, sig_datum.data, sig_datum.size);
        *sigsize = sig_datum.size;
        ret      = true;
      }

      gnutls_free(sig_datum.data);
      gnutls_privkey_deinit(key);
    }
#endif // HAVE_OPENSSL
  }
  else if (alg >= CUPS_JWA_ES256 && alg <= CUPS_JWA_ES512)
  {
    // ECDSA P-256 SHA-256/384/512
    DEBUG_puts("4make_signature: ECDSA signature");

    static unsigned sig_sizes[3] =	// Sizes of signatures
    { 64, 96, 132 };
#ifdef HAVE_OPENSSL
    unsigned char hash[128];		// SHA-256/384/512 hash
    ssize_t	hash_len;		// Length of hash
    unsigned	sig_len;		// Length of signature coordinate
    EC_KEY	*ec;			// EC private key
    ECDSA_SIG	*ec_sig;		// EC signature
    const BIGNUM *r, *s;		// Signature coordinates
    unsigned	r_len, s_len;		// Length of coordinates

    if ((ec = make_ec_key(jwk, false)) != NULL)
    {
      hash_len = cupsHashData(cups_jwa_algorithms[alg], text, text_len, hash, sizeof(hash));
      if ((ec_sig = ECDSA_do_sign(hash, hash_len, ec)) != NULL)
      {
        // Get the raw coordinates...
        ECDSA_SIG_get0(ec_sig, &r, &s);
        r_len    = (unsigned)BN_num_bytes(r);
        s_len    = (unsigned)BN_num_bytes(s);
        *sigsize = sig_sizes[alg - CUPS_JWA_ES256];
        sig_len  = *sigsize / 2;
        ret      = true;

        // 0-pad raw coordinates
        memset(signature, 0, *sigsize);
        BN_bn2bin(r, signature + sig_len - r_len);
        BN_bn2bin(s, signature + *sigsize - s_len);

        // Free the signature
        ECDSA_SIG_free(ec_sig);
      }

      EC_KEY_free(ec);
    }
#else // HAVE_GNUTLS
    if ((key = make_private_key(jwk)) != NULL)
    {
      text_datum.data = (unsigned char *)text;
      text_datum.size = (unsigned)text_len;
      sig_datum.data  = NULL;
      sig_datum.size  = 0;

      if (!gnutls_privkey_sign_data(key, algs[alg - CUPS_JWA_RS256], 0, &text_datum, &sig_datum) && sig_datum.size <= *sigsize)
      {
        gnutls_datum_t	r, s;		// Signature coordinates
        unsigned sig_len;
        *sigsize = sig_sizes[alg - CUPS_JWA_ES256];
	sig_len  = *sigsize / 2;
        gnutls_decode_rs_value(&sig_datum, &r, &s);

        memset(signature, 0, *sigsize);
	if (r.size < sig_len)
          memcpy(signature + sig_len - r.size, r.data, r.size);
	else
          memcpy(signature, r.data + r.size - sig_len, sig_len);
	if (s.size < sig_len)
          memcpy(signature + *sigsize - s.size, s.data, s.size);
	else
          memcpy(signature + sig_len, s.data + s.size - sig_len, sig_len);
        ret = true;

	gnutls_free(r.data);
	gnutls_free(s.data);
      }
      else
      {
	DEBUG_printf("4make_signature: EC signing failed, sig_datum=%d bytes.", (int)sig_datum.size);
      }
      gnutls_free(sig_datum.data);
      gnutls_privkey_deinit(key);
    }
#endif // HAVE_OPENSSL
  }

  done:

  DEBUG_printf("4make_signature: Returning %s.", ret ? "true" : "false");

  free(text);

  if (ret)
    *sigkid = cupsJSONGetString(cupsJSONFind(jwk, "kid"));
  else
    *sigsize = 0;

  return (ret);
}


//
// 'make_string()' - Make a JWT/JWS Compact Serialization string.
//

static char *				// O - JWT/JWS string
make_string(cups_jwt_t *jwt,		// I - JWT object
            bool       with_signature)	// I - Include signature field?
{
  char		*s = NULL,		// JWT/JWS string
		*ptr,			// Pointer into string
		*end;			// End of string
  size_t	jose_len,		// Length of JOSE header
		claims_len,		// Length of claims string
		len;			// Allocation length


  // Get the JOSE header and claims object strings...
  if (!jwt->claims_string)
    jwt->claims_string = cupsJSONExportString(jwt->claims);

  if (!jwt->jose_string || !jwt->claims_string)
    return (NULL);

  jose_len   = strlen(jwt->jose_string);
  claims_len = strlen(jwt->claims_string);

  // Calculate the maximum Base64URL-encoded string length...
  len = ((jose_len + 2) * 4 / 3) + 1 + ((claims_len + 2) * 4 / 3) + 1 + ((_CUPS_JWT_MAX_SIGNATURE + 2) * 4 / 3) + 1;

  if ((s = malloc(len)) == NULL)
    return (NULL);

  ptr = s;
  end = s + len;

  httpEncode64_3(ptr, (size_t)(end - ptr), jwt->jose_string, jose_len, true);
  ptr += strlen(ptr);
  *ptr++ = '.';

  httpEncode64_3(ptr, (size_t)(end - ptr), jwt->claims_string, claims_len, true);
  ptr += strlen(ptr);

  if (with_signature)
  {
    *ptr++ = '.';

    if (jwt->sigsize)
      httpEncode64_3(ptr, (size_t)(end - ptr), (char *)jwt->signature, jwt->sigsize, true);
  }

  return (s);
}
