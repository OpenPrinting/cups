//
// JSON Web Token API definitions for CUPS.
//
// Copyright © 2023-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_JWT_H_
#  define _CUPS_JWT_H_
#  include "json.h"
#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Constants...
//

#define CUPS_JWT_AUD	"aud"		// JWT audience claim
#define CUPS_JWT_EXP	"exp"		// JWT expiration date/time claim
#define CUPS_JWT_IAT	"iat"		// JWT issued at date/time claim
#define CUPS_JWT_ISS	"iss"		// JWT issuer claim (authorization server)
#define CUPS_JWT_JTI	"jti"		// JWT unique identifier claim
#define CUPS_JWT_NAME	"name"		// OpenID display name
#define CUPS_JWT_NBF	"nbf"		// JWT not before date/time claim
#define CUPS_JWT_SUB	"sub"		// JWT subject claim (username/ID)


//
// Types...
//

typedef enum cups_jwa_e			// JSON Web Algorithms @since CUPS 2.5@
{
  CUPS_JWA_NONE,				// No algorithm
  CUPS_JWA_HS256,				// HMAC using SHA-256
  CUPS_JWA_HS384,				// HMAC using SHA-384
  CUPS_JWA_HS512,				// HMAC using SHA-512
  CUPS_JWA_RS256,				// RSASSA-PKCS1-v1_5 using SHA-256
  CUPS_JWA_RS384,				// RSASSA-PKCS1-v1_5 using SHA-384
  CUPS_JWA_RS512,				// RSASSA-PKCS1-v1_5 using SHA-512
  CUPS_JWA_ES256,				// ECDSA using P-256 and SHA-256
  CUPS_JWA_ES384,				// ECDSA using P-384 and SHA-384
  CUPS_JWA_ES512,				// ECDSA using P-521 and SHA-512
  CUPS_JWA_MAX					// @private@ Maximum JWA value
} cups_jwa_t;

typedef enum cups_jws_format_e		// JSON Web Signature Formats @since CUPS 2.5@
{
  CUPS_JWS_FORMAT_COMPACT,			// JWS Compact Serialization
  CUPS_JWS_FORMAT_JSON				// JWS JSON Serialization
} cups_jws_format_t;

typedef struct _cups_jwt_s cups_jwt_t;	// JSON Web Token @since CUPS 2.5@


//
// Functions...
//

extern void		cupsJWTDelete(cups_jwt_t *jwt) _CUPS_PUBLIC;
extern char		*cupsJWTExportString(cups_jwt_t *jwt, cups_jws_format_t format) _CUPS_PUBLIC;
extern cups_jwa_t	cupsJWTGetAlgorithm(cups_jwt_t *jwt) _CUPS_PUBLIC;
extern double		cupsJWTGetClaimNumber(cups_jwt_t *jwt, const char *claim) _CUPS_PUBLIC;
extern const char	*cupsJWTGetClaimString(cups_jwt_t *jwt, const char *claim) _CUPS_PUBLIC;
extern cups_jtype_t	cupsJWTGetClaimType(cups_jwt_t *jwt, const char *claim) _CUPS_PUBLIC;
extern cups_json_t	*cupsJWTGetClaimValue(cups_jwt_t *jwt, const char *claim) _CUPS_PUBLIC;
extern cups_json_t	*cupsJWTGetClaims(cups_jwt_t *jwt) _CUPS_PUBLIC;
extern double		cupsJWTGetHeaderNumber(cups_jwt_t *jwt, const char *claim) _CUPS_PUBLIC;
extern const char	*cupsJWTGetHeaderString(cups_jwt_t *jwt, const char *claim) _CUPS_PUBLIC;
extern cups_jtype_t	cupsJWTGetHeaderType(cups_jwt_t *jwt, const char *claim) _CUPS_PUBLIC;
extern cups_json_t	*cupsJWTGetHeaderValue(cups_jwt_t *jwt, const char *claim) _CUPS_PUBLIC;
extern cups_json_t	*cupsJWTGetHeaders(cups_jwt_t *jwt) _CUPS_PUBLIC;
extern bool		cupsJWTHasValidSignature(cups_jwt_t *jwt, cups_json_t *keys) _CUPS_PUBLIC;
extern cups_jwt_t	*cupsJWTImportString(const char *s, cups_jws_format_t format) _CUPS_PUBLIC;
extern cups_json_t	*cupsJWTMakePrivateKey(cups_jwa_t alg) _CUPS_PUBLIC;
extern cups_json_t	*cupsJWTMakePublicKey(cups_json_t *jwk) _CUPS_PUBLIC;
extern cups_jwt_t	*cupsJWTNew(const char *type) _CUPS_PUBLIC;
extern void		cupsJWTSetClaimNumber(cups_jwt_t *jwt, const char *claim, double value) _CUPS_PUBLIC;
extern void		cupsJWTSetClaimString(cups_jwt_t *jwt, const char *claim, const char *value) _CUPS_PUBLIC;
extern void		cupsJWTSetClaimValue(cups_jwt_t *jwt, const char *claim, cups_json_t *value) _CUPS_PUBLIC;
extern void		cupsJWTSetHeaderNumber(cups_jwt_t *jwt, const char *claim, double value) _CUPS_PUBLIC;
extern void		cupsJWTSetHeaderString(cups_jwt_t *jwt, const char *claim, const char *value) _CUPS_PUBLIC;
extern void		cupsJWTSetHeaderValue(cups_jwt_t *jwt, const char *claim, cups_json_t *value) _CUPS_PUBLIC;
extern bool		cupsJWTSign(cups_jwt_t *jwt, cups_jwa_t alg, cups_json_t *keys) _CUPS_PUBLIC;


#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_JWT_H_
