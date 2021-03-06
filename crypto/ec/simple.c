/* Originally written by Bodo Moeller for the OpenSSL project.
 * ====================================================================
 * Copyright (c) 1998-2005 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 *
 * Portions of the attached software ("Contribution") are developed by
 * SUN MICROSYSTEMS, INC., and are contributed to the OpenSSL project.
 *
 * The Contribution is licensed pursuant to the OpenSSL open source
 * license provided above.
 *
 * The elliptic curve binary polynomial software is originally written by
 * Sheueling Chang Shantz and Douglas Stebila of Sun Microsystems
 * Laboratories. */

#include <openssl/ec.h>

#include <assert.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/mem.h>

#include "internal.h"


/* Most method functions in this file are designed to work with non-trivial
 * representations of field elements if necessary (see ecp_mont.c): while
 * standard modular addition and subtraction are used, the field_mul and
 * field_sqr methods will be used for multiplication.
 *
 * Functions here specifically assume that if a non-trivial representation is
 * used, it is a Montgomery representation (i.e. 'encoding' means multiplying
 * by some factor R). */

unsigned ec_GFp_simple_group_get_degree(const EC_GROUP *group) {
  return BN_num_bits(&group->field);
}

int ec_GFp_simple_point_init(EC_POINT *point) {
  BN_init(&point->X);
  BN_init(&point->Y);
  BN_init(&point->Z);

  return 1;
}

void ec_GFp_simple_point_finish(EC_POINT *point) {
  BN_free(&point->X);
  BN_free(&point->Y);
  BN_free(&point->Z);
}

int ec_GFp_simple_point_copy(EC_POINT *dest, const EC_POINT *src) {
  if (!BN_copy(&dest->X, &src->X) ||
      !BN_copy(&dest->Y, &src->Y) ||
      !BN_copy(&dest->Z, &src->Z)) {
    return 0;
  }

  return 1;
}

int ec_GFp_simple_point_set_to_infinity(EC_POINT *point) {
  BN_zero(&point->Z);
  return 1;
}

int ec_GFp_simple_add(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
                      const EC_POINT *b, BN_CTX *ctx) {
  int (*field_mul)(const EC_GROUP *, BIGNUM *, const BIGNUM *, const BIGNUM *,
                   BN_CTX *);
  int (*field_sqr)(const EC_GROUP *, BIGNUM *, const BIGNUM *, BN_CTX *);
  const BIGNUM *p;
  BN_CTX *new_ctx = NULL;
  BIGNUM *n0, *n1, *n2, *n3, *n4, *n5, *n6;
  int ret = 0;

  if (a == b) {
    return ec_GFp_simple_dbl(group, r, a, ctx);
  }
  if (EC_POINT_is_at_infinity(group, a)) {
    return ec_GFp_simple_point_copy(r, b);
  }
  if (EC_POINT_is_at_infinity(group, b)) {
    return ec_GFp_simple_point_copy(r, a);
  }

  field_mul = group->meth->field_mul;
  field_sqr = group->meth->field_sqr;
  p = &group->field;

  if (ctx == NULL) {
    ctx = new_ctx = BN_CTX_new();
    if (ctx == NULL) {
      return 0;
    }
  }

  BN_CTX_start(ctx);
  n0 = BN_CTX_get(ctx);
  n1 = BN_CTX_get(ctx);
  n2 = BN_CTX_get(ctx);
  n3 = BN_CTX_get(ctx);
  n4 = BN_CTX_get(ctx);
  n5 = BN_CTX_get(ctx);
  n6 = BN_CTX_get(ctx);
  if (n6 == NULL) {
    goto end;
  }

  /* Note that in this function we must not read components of 'a' or 'b'
   * once we have written the corresponding components of 'r'.
   * ('r' might be one of 'a' or 'b'.)
   */

  /* n1, n2 */
  int b_Z_is_one = BN_cmp(&b->Z, &group->one) == 0;

  if (b_Z_is_one) {
    if (!BN_copy(n1, &a->X) || !BN_copy(n2, &a->Y)) {
      goto end;
    }
    /* n1 = X_a */
    /* n2 = Y_a */
  } else {
    if (!field_sqr(group, n0, &b->Z, ctx) ||
        !field_mul(group, n1, &a->X, n0, ctx)) {
      goto end;
    }
    /* n1 = X_a * Z_b^2 */

    if (!field_mul(group, n0, n0, &b->Z, ctx) ||
        !field_mul(group, n2, &a->Y, n0, ctx)) {
      goto end;
    }
    /* n2 = Y_a * Z_b^3 */
  }

  /* n3, n4 */
  int a_Z_is_one = BN_cmp(&a->Z, &group->one) == 0;
  if (a_Z_is_one) {
    if (!BN_copy(n3, &b->X) || !BN_copy(n4, &b->Y)) {
      goto end;
    }
    /* n3 = X_b */
    /* n4 = Y_b */
  } else {
    if (!field_sqr(group, n0, &a->Z, ctx) ||
        !field_mul(group, n3, &b->X, n0, ctx)) {
      goto end;
    }
    /* n3 = X_b * Z_a^2 */

    if (!field_mul(group, n0, n0, &a->Z, ctx) ||
        !field_mul(group, n4, &b->Y, n0, ctx)) {
      goto end;
    }
    /* n4 = Y_b * Z_a^3 */
  }

  /* n5, n6 */
  if (!BN_mod_sub_quick(n5, n1, n3, p) ||
      !BN_mod_sub_quick(n6, n2, n4, p)) {
    goto end;
  }
  /* n5 = n1 - n3 */
  /* n6 = n2 - n4 */

  if (BN_is_zero(n5)) {
    if (BN_is_zero(n6)) {
      /* a is the same point as b */
      BN_CTX_end(ctx);
      ret = ec_GFp_simple_dbl(group, r, a, ctx);
      ctx = NULL;
      goto end;
    } else {
      /* a is the inverse of b */
      BN_zero(&r->Z);
      ret = 1;
      goto end;
    }
  }

  /* 'n7', 'n8' */
  if (!BN_mod_add_quick(n1, n1, n3, p) ||
      !BN_mod_add_quick(n2, n2, n4, p)) {
    goto end;
  }
  /* 'n7' = n1 + n3 */
  /* 'n8' = n2 + n4 */

  /* Z_r */
  if (a_Z_is_one && b_Z_is_one) {
    if (!BN_copy(&r->Z, n5)) {
      goto end;
    }
  } else {
    if (a_Z_is_one) {
      if (!BN_copy(n0, &b->Z)) {
        goto end;
      }
    } else if (b_Z_is_one) {
      if (!BN_copy(n0, &a->Z)) {
        goto end;
      }
    } else if (!field_mul(group, n0, &a->Z, &b->Z, ctx)) {
      goto end;
    }
    if (!field_mul(group, &r->Z, n0, n5, ctx)) {
      goto end;
    }
  }

  /* Z_r = Z_a * Z_b * n5 */

  /* X_r */
  if (!field_sqr(group, n0, n6, ctx) ||
      !field_sqr(group, n4, n5, ctx) ||
      !field_mul(group, n3, n1, n4, ctx) ||
      !BN_mod_sub_quick(&r->X, n0, n3, p)) {
    goto end;
  }
  /* X_r = n6^2 - n5^2 * 'n7' */

  /* 'n9' */
  if (!BN_mod_lshift1_quick(n0, &r->X, p) ||
      !BN_mod_sub_quick(n0, n3, n0, p)) {
    goto end;
  }
  /* n9 = n5^2 * 'n7' - 2 * X_r */

  /* Y_r */
  if (!field_mul(group, n0, n0, n6, ctx) ||
      !field_mul(group, n5, n4, n5, ctx)) {
    goto end; /* now n5 is n5^3 */
  }
  if (!field_mul(group, n1, n2, n5, ctx) ||
      !BN_mod_sub_quick(n0, n0, n1, p)) {
    goto end;
  }
  if (BN_is_odd(n0) && !BN_add(n0, n0, p)) {
    goto end;
  }
  /* now  0 <= n0 < 2*p,  and n0 is even */
  if (!BN_rshift1(&r->Y, n0)) {
    goto end;
  }
  /* Y_r = (n6 * 'n9' - 'n8' * 'n5^3') / 2 */

  ret = 1;

end:
  if (ctx) {
    /* otherwise we already called BN_CTX_end */
    BN_CTX_end(ctx);
  }
  BN_CTX_free(new_ctx);
  return ret;
}

int ec_GFp_simple_dbl(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
                      BN_CTX *ctx) {
  int (*field_mul)(const EC_GROUP *, BIGNUM *, const BIGNUM *, const BIGNUM *,
                   BN_CTX *);
  int (*field_sqr)(const EC_GROUP *, BIGNUM *, const BIGNUM *, BN_CTX *);
  const BIGNUM *p;
  BN_CTX *new_ctx = NULL;
  BIGNUM *n0, *n1, *n2, *n3;
  int ret = 0;

  if (EC_POINT_is_at_infinity(group, a)) {
    BN_zero(&r->Z);
    return 1;
  }

  field_mul = group->meth->field_mul;
  field_sqr = group->meth->field_sqr;
  p = &group->field;

  if (ctx == NULL) {
    ctx = new_ctx = BN_CTX_new();
    if (ctx == NULL) {
      return 0;
    }
  }

  BN_CTX_start(ctx);
  n0 = BN_CTX_get(ctx);
  n1 = BN_CTX_get(ctx);
  n2 = BN_CTX_get(ctx);
  n3 = BN_CTX_get(ctx);
  if (n3 == NULL) {
    goto err;
  }

  /* Note that in this function we must not read components of 'a'
   * once we have written the corresponding components of 'r'.
   * ('r' might the same as 'a'.)
   */

  /* n1 */
  if (BN_cmp(&a->Z, &group->one) == 0) {
    if (!field_sqr(group, n0, &a->X, ctx) ||
        !BN_mod_lshift1_quick(n1, n0, p) ||
        !BN_mod_add_quick(n0, n0, n1, p) ||
        !BN_mod_add_quick(n1, n0, &group->a, p)) {
      goto err;
    }
    /* n1 = 3 * X_a^2 + a_curve */
  } else {
    /* ring: This assumes a == -3. */
    if (!field_sqr(group, n1, &a->Z, ctx) ||
        !BN_mod_add_quick(n0, &a->X, n1, p) ||
        !BN_mod_sub_quick(n2, &a->X, n1, p) ||
        !field_mul(group, n1, n0, n2, ctx) ||
        !BN_mod_lshift1_quick(n0, n1, p) ||
        !BN_mod_add_quick(n1, n0, n1, p)) {
      goto err;
    }
    /* n1 = 3 * (X_a + Z_a^2) * (X_a - Z_a^2)
     *    = 3 * X_a^2 - 3 * Z_a^4 */
  }

  /* Z_r */
  if (BN_cmp(&a->Z, &group->one) == 0) {
    if (!BN_copy(n0, &a->Y)) {
      goto err;
    }
  } else if (!field_mul(group, n0, &a->Y, &a->Z, ctx)) {
    goto err;
  }
  if (!BN_mod_lshift1_quick(&r->Z, n0, p)) {
    goto err;
  }
  /* Z_r = 2 * Y_a * Z_a */

  /* n2 */
  if (!field_sqr(group, n3, &a->Y, ctx) ||
      !field_mul(group, n2, &a->X, n3, ctx) ||
      !BN_mod_lshift_quick(n2, n2, 2, p)) {
    goto err;
  }
  /* n2 = 4 * X_a * Y_a^2 */

  /* X_r */
  if (!BN_mod_lshift1_quick(n0, n2, p) ||
      !field_sqr(group, &r->X, n1, ctx) ||
      !BN_mod_sub_quick(&r->X, &r->X, n0, p)) {
    goto err;
  }
  /* X_r = n1^2 - 2 * n2 */

  /* n3 */
  if (!field_sqr(group, n0, n3, ctx) ||
      !BN_mod_lshift_quick(n3, n0, 3, p)) {
    goto err;
  }
  /* n3 = 8 * Y_a^4 */

  /* Y_r */
  if (!BN_mod_sub_quick(n0, n2, &r->X, p) ||
      !field_mul(group, n0, n1, n0, ctx) ||
      !BN_mod_sub_quick(&r->Y, n0, n3, p)) {
    goto err;
  }
  /* Y_r = n1 * (n2 - X_r) - n3 */

  ret = 1;

err:
  BN_CTX_end(ctx);
  BN_CTX_free(new_ctx);
  return ret;
}

int ec_GFp_simple_invert(const EC_GROUP *group, EC_POINT *point) {
  if (EC_POINT_is_at_infinity(group, point) || BN_is_zero(&point->Y)) {
    /* point is its own inverse */
    return 1;
  }

  return BN_usub(&point->Y, &group->field, &point->Y);
}

int ec_GFp_simple_is_at_infinity(const EC_POINT *point) {
  return BN_is_zero(&point->Z);
}
