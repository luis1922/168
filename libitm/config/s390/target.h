/* Copyright (C) 2013 Free Software Foundation, Inc.
   Contributed by Andreas Krebbel <krebbel@linux.vnet.ibm.com>.

   This file is part of the GNU Transactional Memory Library (libitm).

   Libitm is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   Libitm is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */


#include <htmintrin.h>

/* Number of retries for transient failures.  */
#define _HTM_ITM_RETRIES 10

namespace GTM HIDDEN {

#define HW_CACHELINE_SIZE 256

typedef struct gtm_jmpbuf
{
  /* We save registers r6-r14.  */
  long int __gregs[9];
  /* r15 is stored into cfa field.  It needs to be named that way
     since tls.h is accessing the field by name.  Be aware that this
     is not actually what we consider the CFA on s390 (since there is
     a fix offset between sp and CFA).  It is merely the saved stack
     pointer.  */
  void *cfa;

#ifdef __s390x__
  /* We save fpu registers f8 - f15.  */
  long __fpregs[8];
#else
  /* We save fpu registers f4 and f6.  */
  long __fpregs[4];
#endif
} gtm_jmpbuf;

static inline void
cpu_relax (void)
{
  __asm volatile ("" : : : "memory");
}

#ifdef __HTM__
#define USE_HTM_FASTPATH

static inline bool
htm_available ()
{
  return true;
}

static inline uint32_t
htm_init ()
{
  return htm_available () ? _HTM_ITM_RETRIES : 0;
}

static inline uint32_t
htm_begin ()
{
  return __builtin_tbegin_nofloat (NULL);
}

static inline bool
htm_begin_success (uint32_t begin_ret)
{
  return begin_ret == _HTM_TBEGIN_STARTED;
}

static inline void
htm_commit ()
{
  __builtin_tend ();
}

static inline void
htm_abort ()
{
  __builtin_tabort (_HTM_FIRST_USER_ABORT_CODE);
}

static inline bool
htm_abort_should_retry (uint32_t begin_ret)
{
  return begin_ret == _HTM_TBEGIN_TRANSIENT;
}

static inline bool
htm_transaction_active ()
{
  return __builtin_tx_nesting_depth() != 0;
}

#endif

} // namespace GTM
