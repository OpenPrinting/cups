//
// JWT API unit tests for CUPS.
//
// Copyright © 2023-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups.h"
#include "jwt.h"
#include "test-internal.h"


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int		i;			// Looping var
  cups_jwt_t	*jwt;			// JSON Web Token object
  cups_json_t	*jwk;			// JSON Web Key Set


  if (argc == 1)
  {
    // Do unit tests...
    cups_json_t	*pubjwk;		// Public key set
    char	*temp;			// Temporary string
    static const char * const examples[][2] =
    {					// JWT examples
      {
        "eyJ0eXAiOiJKV1QiLA0KICJhbGciOiJIUzI1NiJ9."
	    "eyJpc3MiOiJqb2UiLA0KICJleHAiOjEzMDA4MTkzODAsDQo"
	    "gImh0dHA6Ly9leGFtcGxlLmNvbS9pc19yb290Ijp0cnVlfQ."
	    "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk",
	"{\"kty\":\"oct\","
	    "\"k\":\"AyM1SysPpbyDfgZld3umj1qzKObwVMkoqQ-EstJQLr_T-1qS0gZH75"
	    "aKtMN3Yj0iPS4hcgUuTwjAzZr1Z9CAow\"}"
      },
      {
        "eyJhbGciOiJSUzI1NiJ9."
            "eyJpc3MiOiJqb2UiLA0KICJleHAiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFt"
            "cGxlLmNvbS9pc19yb290Ijp0cnVlfQ."
            "cC4hiUPoj9Eetdgtv3hF80EGrhuB__dzERat0XF9g2VtQgr9PJbu3XOiZj5RZmh7"
            "AAuHIm4Bh-0Qc_lF5YKt_O8W2Fp5jujGbds9uJdbF9CUAr7t1dnZcAcQjbKBYNX4"
            "BAynRFdiuB--f_nZLgrnbyTyWzO75vRK5h6xBArLIARNPvkSjtQBMHlb1L07Qe7K"
            "0GarZRmB_eSN9383LcOLn6_dO--xi12jzDwusC-eOkHWEsqtFZESc6BfI7noOPqv"
            "hJ1phCnvWh6IeYI2w9QOYEUipUTI8np6LbgGY9Fs98rqVt5AXLIhWkWywlVmtVrB"
            "p0igcN_IoypGlUPQGe77Rw",
	"{\"kty\":\"RSA\","
            "\"n\":\"ofgWCuLjybRlzo0tZWJjNiuSfb4p4fAkd_wWJcyQoTbji9k0l8W26mPddx"
            "HmfHQp-Vaw-4qPCJrcS2mJPMEzP1Pt0Bm4d4QlL-yRT-SFd2lZS-pCgNMs"
            "D1W_YpRPEwOWvG6b32690r2jZ47soMZo9wGzjb_7OMg0LOL-bSf63kpaSH"
            "SXndS5z5rexMdbBYUsLA9e-KXBdQOS-UTo7WTBEMa2R2CapHg665xsmtdV"
            "MTBQY4uDZlxvb3qCo5ZwKh9kG4LT6_I5IhlJH7aGhyxXFvUK-DWNmoudF8"
            "NAco9_h9iaGNj8q2ethFkMLs91kzk2PAcDTW9gb54h4FRWyuXpoQ\","
            "\"e\":\"AQAB\","
            "\"d\":\"Eq5xpGnNCivDflJsRQBXHx1hdR1k6Ulwe2JZD50LpXyWPEAeP88vLNO97I"
            "jlA7_GQ5sLKMgvfTeXZx9SE-7YwVol2NXOoAJe46sui395IW_GO-pWJ1O0"
            "BkTGoVEn2bKVRUCgu-GjBVaYLU6f3l9kJfFNS3E0QbVdxzubSu3Mkqzjkn"
            "439X0M_V51gfpRLI9JYanrC4D4qAdGcopV_0ZHHzQlBjudU2QvXt4ehNYT"
            "CBr6XCLQUShb1juUO1ZdiYoFaFQT5Tw8bGUl_x_jTj3ccPDVZFD9pIuhLh"
            "BOneufuBiB4cS98l2SR_RQyGWSeWjnczT0QU91p1DhOVRuOopznQ\","
            "\"p\":\"4BzEEOtIpmVdVEZNCqS7baC4crd0pqnRH_5IB3jw3bcxGn6QLvnEtfdUdi"
            "YrqBdss1l58BQ3KhooKeQTa9AB0Hw_Py5PJdTJNPY8cQn7ouZ2KKDcmnPG"
            "BY5t7yLc1QlQ5xHdwW1VhvKn-nXqhJTBgIPgtldC-KDV5z-y2XDwGUc\","
            "\"q\":\"uQPEfgmVtjL0Uyyx88GZFF1fOunH3-7cepKmtH4pxhtCoHqpWmT8YAmZxa"
            "ewHgHAjLYsp1ZSe7zFYHj7C6ul7TjeLQeZD_YwD66t62wDmpe_HlB-TnBA"
            "-njbglfIsRLtXlnDzQkv5dTltRJ11BKBBypeeF6689rjcJIDEz9RWdc\","
            "\"dp\":\"BwKfV3Akq5_MFZDFZCnW-wzl-CCo83WoZvnLQwCTeDv8uzluRSnm71I3Q"
            "CLdhrqE2e9YkxvuxdBfpT_PI7Yz-FOKnu1R6HsJeDCjn12Sk3vmAktV2zb"
            "34MCdy7cpdTh_YVr7tss2u6vneTwrA86rZtu5Mbr1C1XsmvkxHQAdYo0\","
            "\"dq\":\"h_96-mK1R_7glhsum81dZxjTnYynPbZpHziZjeeHcXYsXaaMwkOlODsWa"
            "7I9xXDoRwbKgB719rrmI2oKr6N3Do9U0ajaHF-NKJnwgjMd2w9cjz3_-ky"
            "NlxAr2v4IKhGNpmM5iIgOS1VZnOZ68m6_pbLBSp3nssTdlqvd0tIiTHU\","
            "\"qi\":\"IYd7DHOhrWvxkwPQsRM2tOgrjbcrfvtQJipd-DlcxyVuuM9sQLdgjVk2o"
            "y26F0EmpScGLq2MowX7fhd_QJQ3ydy5cY7YIBi87w93IKLEdfnbJtoOPLU"
            "W0ITrJReOgo1cq9SbsxYawBgfp_gh6A5603k2-ZQwVK0JKSHuLFkuQ3U\""
            "}"
      },
      {
        "eyJhbGciOiJFUzI1NiJ9."
            "eyJpc3MiOiJqb2UiLA0KICJleHAiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFt"
            "cGxlLmNvbS9pc19yb290Ijp0cnVlfQ."
            "DtEhU3ljbEg8L38VWAfUAqOyKAM6-Xx-F4GawxaepmXFCgfTjDxw5djxLa8ISlSA"
            "pmWQxfKTUJqPP3-Kg6NU1Q",
	"{\"kty\":\"EC\","
            "\"crv\":\"P-256\","
            "\"x\":\"f83OJ3D2xF1Bg8vub9tLe1gHMzV76e8Tus9uPHvRVEU\","
            "\"y\":\"x_FEzRu9m36HLN_tue659LNpXW6pCyStikYjKIWI5a0\","
            "\"d\":\"jpsQnnGQmL-YBIffH1136cspYG6-0iY7X1fCE9-E9LI\""
            "}"
      }
    };

    testBegin("cupsJWTNew(NULL)");
    jwt = cupsJWTNew(NULL);
    testEnd(jwt != NULL);

    testBegin("cupsJWTSetClaimNumber(CUPS_JWT_IAT)");
    cupsJWTSetClaimNumber(jwt, CUPS_JWT_IAT, (double)time(NULL) + 86400.0);
    testEnd(cupsJWTGetClaimNumber(jwt, CUPS_JWT_IAT) > 0.0);

    testBegin("cupsJWTSetClaimString(CUPS_JWT_SUB)");
    cupsJWTSetClaimString(jwt, CUPS_JWT_SUB, "joe.user");
    testEnd(cupsJWTGetClaimString(jwt, CUPS_JWT_SUB) != NULL);

    testBegin("cupsJWTMakePrivateKey(HS256)");
    jwk = cupsJWTMakePrivateKey(CUPS_JWA_HS256);
    testEnd(jwk != NULL);

    if ((temp = cupsJSONExportString(jwk)) != NULL)
    {
      puts(temp);
      free(temp);
    }

    testBegin("cupsJWTSign(HS256)");
    testEnd(cupsJWTSign(jwt, CUPS_JWA_HS256, jwk));

    testBegin("cupsJWTHasValidSignature(HS256)");
    testEnd(cupsJWTHasValidSignature(jwt, jwk));

    cupsJSONDelete(jwk);

    testBegin("cupsJWTMakePrivateKey(RS256)");
    jwk = cupsJWTMakePrivateKey(CUPS_JWA_RS256);
    testEnd(jwk != NULL);

    if ((temp = cupsJSONExportString(jwk)) != NULL)
    {
      puts(temp);
      free(temp);
    }

    testBegin("cupsJWTMakePublicKey(RS256)");
    pubjwk = cupsJWTMakePublicKey(jwk);
    testEnd(pubjwk != NULL);

    if ((temp = cupsJSONExportString(pubjwk)) != NULL)
    {
      puts(temp);
      free(temp);
    }

    testBegin("cupsJWTSign(RS256)");
    testEnd(cupsJWTSign(jwt, CUPS_JWA_RS256, jwk));

    testBegin("cupsJWTHasValidSignature(RS256)");
    testEnd(cupsJWTHasValidSignature(jwt, pubjwk));

    testBegin("cupsJWTSign(RS384)");
    testEnd(cupsJWTSign(jwt, CUPS_JWA_RS384, jwk));

    testBegin("cupsJWTHasValidSignature(RS384)");
    testEnd(cupsJWTHasValidSignature(jwt, pubjwk));

    testBegin("cupsJWTSign(RS512)");
    testEnd(cupsJWTSign(jwt, CUPS_JWA_RS512, jwk));

    testBegin("cupsJWTHasValidSignature(RS512)");
    testEnd(cupsJWTHasValidSignature(jwt, pubjwk));

    cupsJSONDelete(jwk);
    cupsJSONDelete(pubjwk);

    testBegin("cupsJWTMakePrivateKey(ES256)");
    jwk = cupsJWTMakePrivateKey(CUPS_JWA_ES256);
    testEnd(jwk != NULL);

    if ((temp = cupsJSONExportString(jwk)) != NULL)
    {
      puts(temp);
      free(temp);
    }

    testBegin("cupsJWTMakePublicKey(ES256)");
    pubjwk = cupsJWTMakePublicKey(jwk);
    testEnd(pubjwk != NULL);

    if ((temp = cupsJSONExportString(pubjwk)) != NULL)
    {
      puts(temp);
      free(temp);
    }

    testBegin("cupsJWTSign(ES256)");
    testEnd(cupsJWTSign(jwt, CUPS_JWA_ES256, jwk));

    testBegin("cupsJWTHasValidSignature(ES256)");
    testEnd(cupsJWTHasValidSignature(jwt, pubjwk));

    cupsJSONDelete(jwk);
    cupsJSONDelete(pubjwk);

    testBegin("cupsJWTMakePrivateKey(ES384)");
    jwk = cupsJWTMakePrivateKey(CUPS_JWA_ES384);
    testEnd(jwk != NULL);

    if ((temp = cupsJSONExportString(jwk)) != NULL)
    {
      puts(temp);
      free(temp);
    }

    testBegin("cupsJWTMakePublicKey(ES384)");
    pubjwk = cupsJWTMakePublicKey(jwk);
    testEnd(pubjwk != NULL);

    if ((temp = cupsJSONExportString(pubjwk)) != NULL)
    {
      puts(temp);
      free(temp);
    }

    testBegin("cupsJWTSign(ES384)");
    testEnd(cupsJWTSign(jwt, CUPS_JWA_ES384, jwk));

    testBegin("cupsJWTHasValidSignature(ES384)");
    testEnd(cupsJWTHasValidSignature(jwt, pubjwk));

    cupsJSONDelete(jwk);
    cupsJSONDelete(pubjwk);

    testBegin("cupsJWTMakePrivateKey(ES512)");
    jwk = cupsJWTMakePrivateKey(CUPS_JWA_ES512);
    testEnd(jwk != NULL);

    if ((temp = cupsJSONExportString(jwk)) != NULL)
    {
      puts(temp);
      free(temp);
    }

    testBegin("cupsJWTMakePublicKey(ES512)");
    pubjwk = cupsJWTMakePublicKey(jwk);
    testEnd(pubjwk != NULL);

    if ((temp = cupsJSONExportString(pubjwk)) != NULL)
    {
      puts(temp);
      free(temp);
    }

    testBegin("cupsJWTSign(ES512)");
    testEnd(cupsJWTSign(jwt, CUPS_JWA_ES512, jwk));

    testBegin("cupsJWTHasValidSignature(ES512)");
    testEnd(cupsJWTHasValidSignature(jwt, pubjwk));

    cupsJSONDelete(jwk);
    cupsJSONDelete(pubjwk);

    testBegin("cupsJWTDelete()");
    cupsJWTDelete(jwt);
    testEnd(true);

    for (i = 0; i < (int)(sizeof(examples) / sizeof(examples[0])); i ++)
    {
      testBegin("cupsJWTImportString(\"%s\", CUPS_JWS_FORMAT_COMPACT)", examples[i][0]);
      if ((jwt = cupsJWTImportString(examples[i][0], CUPS_JWS_FORMAT_COMPACT)) != NULL)
      {
        testEnd(true);

        testBegin("cupsJSONImportString(\"%s\")", examples[i][1]);
        if ((jwk = cupsJSONImportString(examples[i][1])) != NULL)
        {
          testEnd(true);
          testBegin("cupsJWTHasValidSignature()");
          testEnd(cupsJWTHasValidSignature(jwt, jwk));
        }
        else
        {
          testEndMessage(false, "%s", cupsGetErrorString());
        }
      }
      else
      {
        testEndMessage(false, "%s", cupsGetErrorString());
      }

      cupsJSONDelete(jwk);
      cupsJWTDelete(jwt);
    }

    if (!testsPassed)
      return (1);
  }
  else
  {
    // Try loading JWT string on the command-line...
    cups_json_t	*jwks = NULL;		// JWT Key Set, if any

    for (i = 1; i < argc; i ++)
    {
      if (!access(argv[i], R_OK))
      {
        if ((jwks = cupsJSONImportFile(argv[i])) == NULL)
        {
	  fprintf(stderr, "%s: %s\n", argv[i], cupsGetErrorString());
	  return (1);
        }
      }
      else if ((jwt = cupsJWTImportString(argv[i], CUPS_JWS_FORMAT_COMPACT)) != NULL)
      {
        cups_json_t	*claims = cupsJWTGetClaims(jwt);
					// All claims
        cups_json_t	*headers = cupsJWTGetHeaders(jwt);
					// All JOSE headers
        char		*temp;		// Temporary string
	const char	*aud = cupsJWTGetClaimString(jwt, CUPS_JWT_AUD);
					// Audience
	const char	*iss = cupsJWTGetClaimString(jwt, CUPS_JWT_ISS);
					// Issuer
	const char	*jti = cupsJWTGetClaimString(jwt, CUPS_JWT_JTI);
					// JWT ID
	const char	*name = cupsJWTGetClaimString(jwt, CUPS_JWT_NAME);
					// Display name
	const char	*sub = cupsJWTGetClaimString(jwt, CUPS_JWT_SUB);
					// Subject (username/ID)
	double		iat = cupsJWTGetClaimNumber(jwt, CUPS_JWT_IAT);
					// Issue time
	double		exp = cupsJWTGetClaimNumber(jwt, CUPS_JWT_EXP);
					// Expiration time
	double		nbf = cupsJWTGetClaimNumber(jwt, CUPS_JWT_NBF);
					// Not before time
	char		date[256];	// Date

	if (iss)
	  printf("Issuer: %s\n", iss);
	if (name)
	  printf("Display Name: %s\n", name);
	if (sub)
	  printf("Subject: %s\n", sub);
	if (aud)
	  printf("Audience: %s\n", aud);
	if (jti)
	  printf("JWT ID: %s\n", jti);
	if (iat > 0.0)
	  printf("Issued On: %s\n", httpGetDateString2((time_t)iat, date, sizeof(date)));
	if (exp > 0.0)
	  printf("Expires On: %s\n", httpGetDateString2((time_t)exp, date, sizeof(date)));
	if (nbf > 0.0)
	  printf("Not Before: %s\n", httpGetDateString2((time_t)nbf, date, sizeof(date)));
        printf("Valid: %s\n", jwks ? (cupsJWTHasValidSignature(jwt, jwks) ? "yes" : "no") : "unknown");

        if ((temp = cupsJSONExportString(headers)) != NULL)
        {
          printf("\njose=%s\n", temp);
          free(temp);
	}

        if ((temp = cupsJSONExportString(claims)) != NULL)
        {
          printf("\nclaims=%s\n", temp);
          free(temp);
	}

        cupsJWTDelete(jwt);
      }
      else
      {
	fprintf(stderr, "%s: %s\n", argv[i], cupsGetErrorString());
	return (1);
      }
    }
  }

  return (0);
}
