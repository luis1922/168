;; Decimal Floating Point (DFP) patterns.
;; Copyright (C) 2007-2013 Free Software Foundation, Inc.
;; Contributed by Ben Elliston (bje@au.ibm.com) and Peter Bergner
;; (bergner@vnet.ibm.com).

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 3, or (at your
;; option) any later version.

;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.

;;
;; UNSPEC usage
;;

(define_c_enum "unspec"
  [UNSPEC_MOVSD_LOAD
   UNSPEC_MOVSD_STORE
  ])


(define_insn "movsd_store"
  [(set (match_operand:DD 0 "nonimmediate_operand" "=m")
	(unspec:DD [(match_operand:SD 1 "input_operand" "d")]
		   UNSPEC_MOVSD_STORE))]
  "(gpc_reg_operand (operands[0], DDmode)
   || gpc_reg_operand (operands[1], SDmode))
   && TARGET_HARD_FLOAT && TARGET_FPRS"
  "stfd%U0%X0 %1,%0"
  [(set (attr "type")
      (if_then_else
	(match_test "update_indexed_address_mem (operands[0], VOIDmode)")
	(const_string "fpstore_ux")
	(if_then_else
	  (match_test "update_address_mem (operands[0], VOIDmode)")
	  (const_string "fpstore_u")
	  (const_string "fpstore"))))
   (set_attr "length" "4")])

(define_insn "movsd_load"
  [(set (match_operand:SD 0 "nonimmediate_operand" "=f")
	(unspec:SD [(match_operand:DD 1 "input_operand" "m")]
		   UNSPEC_MOVSD_LOAD))]
  "(gpc_reg_operand (operands[0], SDmode)
   || gpc_reg_operand (operands[1], DDmode))
   && TARGET_HARD_FLOAT && TARGET_FPRS"
  "lfd%U1%X1 %0,%1"
  [(set (attr "type")
      (if_then_else
	(match_test "update_indexed_address_mem (operands[1], VOIDmode)")
	(const_string "fpload_ux")
	(if_then_else
	  (match_test "update_address_mem (operands[1], VOIDmode)")
	  (const_string "fpload_u")
	  (const_string "fpload"))))
   (set_attr "length" "4")])

;; Hardware support for decimal floating point operations.

(define_insn "extendsddd2"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(float_extend:DD (match_operand:SD 1 "gpc_reg_operand" "f")))]
  "TARGET_DFP"
  "dctdp %0,%1"
  [(set_attr "type" "fp")])

(define_expand "extendsdtd2"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(float_extend:TD (match_operand:SD 1 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
{
  rtx tmp = gen_reg_rtx (DDmode);
  emit_insn (gen_extendsddd2 (tmp, operands[1]));
  emit_insn (gen_extendddtd2 (operands[0], tmp));
  DONE;
})

(define_insn "truncddsd2"
  [(set (match_operand:SD 0 "gpc_reg_operand" "=f")
	(float_truncate:SD (match_operand:DD 1 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "drsp %0,%1"
  [(set_attr "type" "fp")])

(define_expand "negdd2"
  [(set (match_operand:DD 0 "gpc_reg_operand" "")
	(neg:DD (match_operand:DD 1 "gpc_reg_operand" "")))]
  "TARGET_HARD_FLOAT && TARGET_FPRS"
  "")

(define_insn "*negdd2_fpr"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(neg:DD (match_operand:DD 1 "gpc_reg_operand" "d")))]
  "TARGET_HARD_FLOAT && TARGET_FPRS"
  "fneg %0,%1"
  [(set_attr "type" "fp")])

(define_expand "absdd2"
  [(set (match_operand:DD 0 "gpc_reg_operand" "")
	(abs:DD (match_operand:DD 1 "gpc_reg_operand" "")))]
  "TARGET_HARD_FLOAT && TARGET_FPRS"
  "")

(define_insn "*absdd2_fpr"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(abs:DD (match_operand:DD 1 "gpc_reg_operand" "d")))]
  "TARGET_HARD_FLOAT && TARGET_FPRS"
  "fabs %0,%1"
  [(set_attr "type" "fp")])

(define_insn "*nabsdd2_fpr"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(neg:DD (abs:DD (match_operand:DD 1 "gpc_reg_operand" "d"))))]
  "TARGET_HARD_FLOAT && TARGET_FPRS"
  "fnabs %0,%1"
  [(set_attr "type" "fp")])

(define_expand "negtd2"
  [(set (match_operand:TD 0 "gpc_reg_operand" "")
	(neg:TD (match_operand:TD 1 "gpc_reg_operand" "")))]
  "TARGET_HARD_FLOAT && TARGET_FPRS"
  "")

(define_insn "*negtd2_fpr"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(neg:TD (match_operand:TD 1 "gpc_reg_operand" "d")))]
  "TARGET_HARD_FLOAT && TARGET_FPRS"
  "fneg %0,%1"
  [(set_attr "type" "fp")])

(define_expand "abstd2"
  [(set (match_operand:TD 0 "gpc_reg_operand" "")
	(abs:TD (match_operand:TD 1 "gpc_reg_operand" "")))]
  "TARGET_HARD_FLOAT && TARGET_FPRS"
  "")

(define_insn "*abstd2_fpr"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(abs:TD (match_operand:TD 1 "gpc_reg_operand" "d")))]
  "TARGET_HARD_FLOAT && TARGET_FPRS"
  "fabs %0,%1"
  [(set_attr "type" "fp")])

(define_insn "*nabstd2_fpr"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(neg:TD (abs:TD (match_operand:TD 1 "gpc_reg_operand" "d"))))]
  "TARGET_HARD_FLOAT && TARGET_FPRS"
  "fnabs %0,%1"
  [(set_attr "type" "fp")])

;; Hardware support for decimal floating point operations.

(define_insn "extendddtd2"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(float_extend:TD (match_operand:DD 1 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dctqpq %0,%1"
  [(set_attr "type" "fp")])

;; The result of drdpq is an even/odd register pair with the converted
;; value in the even register and zero in the odd register.
;; FIXME: Avoid the register move by using a reload constraint to ensure
;; that the result is the first of the pair receiving the result of drdpq.

(define_insn "trunctddd2"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(float_truncate:DD (match_operand:TD 1 "gpc_reg_operand" "d")))
   (clobber (match_scratch:TD 2 "=d"))]
  "TARGET_DFP"
  "drdpq %2,%1\;fmr %0,%2"
  [(set_attr "type" "fp")])

(define_insn "adddd3"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(plus:DD (match_operand:DD 1 "gpc_reg_operand" "%d")
		 (match_operand:DD 2 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dadd %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "addtd3"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(plus:TD (match_operand:TD 1 "gpc_reg_operand" "%d")
		 (match_operand:TD 2 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "daddq %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "subdd3"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(minus:DD (match_operand:DD 1 "gpc_reg_operand" "d")
		  (match_operand:DD 2 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dsub %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "subtd3"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(minus:TD (match_operand:TD 1 "gpc_reg_operand" "d")
		  (match_operand:TD 2 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dsubq %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "muldd3"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(mult:DD (match_operand:DD 1 "gpc_reg_operand" "%d")
		 (match_operand:DD 2 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dmul %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "multd3"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(mult:TD (match_operand:TD 1 "gpc_reg_operand" "%d")
		 (match_operand:TD 2 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dmulq %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "divdd3"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(div:DD (match_operand:DD 1 "gpc_reg_operand" "d")
		(match_operand:DD 2 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "ddiv %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "divtd3"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(div:TD (match_operand:TD 1 "gpc_reg_operand" "d")
		(match_operand:TD 2 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "ddivq %0,%1,%2"
  [(set_attr "type" "fp")])

(define_insn "*cmpdd_internal1"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(compare:CCFP (match_operand:DD 1 "gpc_reg_operand" "d")
		      (match_operand:DD 2 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dcmpu %0,%1,%2"
  [(set_attr "type" "fpcompare")])

(define_insn "*cmptd_internal1"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(compare:CCFP (match_operand:TD 1 "gpc_reg_operand" "d")
		      (match_operand:TD 2 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dcmpuq %0,%1,%2"
  [(set_attr "type" "fpcompare")])

(define_insn "floatdidd2"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(float:DD (match_operand:DI 1 "gpc_reg_operand" "d")))]
  "TARGET_DFP && TARGET_POPCNTD"
  "dcffix %0,%1"
  [(set_attr "type" "fp")])

(define_insn "floatditd2"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(float:TD (match_operand:DI 1 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dcffixq %0,%1"
  [(set_attr "type" "fp")])

;; Convert a decimal64 to a decimal64 whose value is an integer.
;; This is the first stage of converting it to an integer type.

(define_insn "ftruncdd2"
  [(set (match_operand:DD 0 "gpc_reg_operand" "=d")
	(fix:DD (match_operand:DD 1 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "drintn. 0,%0,%1,1"
  [(set_attr "type" "fp")])

;; Convert a decimal64 whose value is an integer to an actual integer.
;; This is the second stage of converting decimal float to integer type.

(define_insn "fixdddi2"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=d")
	(fix:DI (match_operand:DD 1 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dctfix %0,%1"
  [(set_attr "type" "fp")])

;; Convert a decimal128 to a decimal128 whose value is an integer.
;; This is the first stage of converting it to an integer type.

(define_insn "ftrunctd2"
  [(set (match_operand:TD 0 "gpc_reg_operand" "=d")
	(fix:TD (match_operand:TD 1 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "drintnq. 0,%0,%1,1"
  [(set_attr "type" "fp")])

;; Convert a decimal128 whose value is an integer to an actual integer.
;; This is the second stage of converting decimal float to integer type.

(define_insn "fixtddi2"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=d")
	(fix:DI (match_operand:TD 1 "gpc_reg_operand" "d")))]
  "TARGET_DFP"
  "dctfixq %0,%1"
  [(set_attr "type" "fp")])
