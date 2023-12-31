/* This file contains routines to construct and validate Cilk Plus
   constructs within the C and C++ front ends.

   Copyright (C) 2011-2013  Free Software Foundation, Inc.
   Contributed by Balaji V. Iyer <balaji.v.iyer@intel.com>,
		  Aldy Hernandez <aldyh@redhat.com>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "c-common.h"

/* Helper function for c_check_cilk_loop.

   Validate the increment in a _Cilk_for construct or a <#pragma simd>
   for loop.

   LOC is the location of the `for' keyword.  DECL is the induction
   variable.  INCR is the original increment expression.

   Returns the canonicalized increment expression for an OMP_FOR_INCR.
   If there is a validation error, returns error_mark_node.  */

static tree
c_check_cilk_loop_incr (location_t loc, tree decl, tree incr)
{
  if (EXPR_HAS_LOCATION (incr))
    loc = EXPR_LOCATION (incr);

  if (!incr)
    {
      error_at (loc, "missing increment");
      return error_mark_node;
    }

  switch (TREE_CODE (incr))
    {
    case POSTINCREMENT_EXPR:
    case PREINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case PREDECREMENT_EXPR:
      if (TREE_OPERAND (incr, 0) != decl)
	break;

      // Bah... canonicalize into whatever OMP_FOR_INCR needs.
      if (POINTER_TYPE_P (TREE_TYPE (decl))
	  && TREE_OPERAND (incr, 1))
	{
	  tree t = fold_convert_loc (loc,
				     sizetype, TREE_OPERAND (incr, 1));

	  if (TREE_CODE (incr) == POSTDECREMENT_EXPR
	      || TREE_CODE (incr) == PREDECREMENT_EXPR)
	    t = fold_build1_loc (loc, NEGATE_EXPR, sizetype, t);
	  t = fold_build_pointer_plus (decl, t);
	  incr = build2 (MODIFY_EXPR, void_type_node, decl, t);
	}
      return incr;

    case MODIFY_EXPR:
      {
	tree rhs;

	if (TREE_OPERAND (incr, 0) != decl)
	  break;

	rhs = TREE_OPERAND (incr, 1);
	if (TREE_CODE (rhs) == PLUS_EXPR
	    && (TREE_OPERAND (rhs, 0) == decl
		|| TREE_OPERAND (rhs, 1) == decl)
	    && INTEGRAL_TYPE_P (TREE_TYPE (rhs)))
	  return incr;
	else if (TREE_CODE (rhs) == MINUS_EXPR
		 && TREE_OPERAND (rhs, 0) == decl
		 && INTEGRAL_TYPE_P (TREE_TYPE (rhs)))
	  return incr;
	// Otherwise fail because only PLUS_EXPR and MINUS_EXPR are
	// allowed.
	break;
      }

    default:
      break;
    }

  error_at (loc, "invalid increment expression");
  return error_mark_node;
}

/* Callback for walk_tree to validate the body of a pragma simd loop
   or _cilk_for loop.

   This function is passed in as a function pointer to walk_tree.  *TP is
   the current tree pointer, *WALK_SUBTREES is set to 0 by this function if
   recursing into TP's subtrees is unnecessary. *DATA is a bool variable that
   is set to false if an error has occured.  */

tree
c_validate_cilk_plus_loop (tree *tp, int *walk_subtrees, void *data)
{
  if (!tp || !*tp)
    return NULL_TREE;

  bool *valid = (bool *) data;

  switch (TREE_CODE (*tp))
    {
    case CALL_EXPR:
      {
	tree fndecl = CALL_EXPR_FN (*tp);

	if (TREE_CODE (fndecl) == ADDR_EXPR)
	  fndecl = TREE_OPERAND (fndecl, 0);
	if (TREE_CODE (fndecl) == FUNCTION_DECL)
	  {
	    if (setjmp_call_p (fndecl))
	      {
		error_at (EXPR_LOCATION (*tp),
			  "calls to setjmp are not allowed within loops "
			  "annotated with #pragma simd");
		*valid = false;
		*walk_subtrees = 0;
	      }
	  }
	break;
      }

    case OMP_PARALLEL:
    case OMP_TASK:
    case OMP_FOR:
    case OMP_SIMD:
    case OMP_DISTRIBUTE:
    case OMP_SECTIONS:
    case OMP_SINGLE:
    case OMP_SECTION:
    case OMP_MASTER:
    case OMP_ORDERED:
    case OMP_CRITICAL:
    case OMP_ATOMIC:
    case OMP_ATOMIC_READ:
    case OMP_ATOMIC_CAPTURE_OLD:
    case OMP_ATOMIC_CAPTURE_NEW:
      error_at (EXPR_LOCATION (*tp), "OpenMP statements are not allowed "
		"within loops annotated with #pragma simd");
      *valid = false;
      *walk_subtrees = 0;
      break;

    default:
      break;
    }
  return NULL_TREE;
}  

/* Validate the body of a _Cilk_for construct or a <#pragma simd> for
   loop.

   Returns true if there were no errors, false otherwise.  */

static bool
c_check_cilk_loop_body (tree body)
{
  bool valid = true;
  walk_tree (&body, c_validate_cilk_plus_loop, (void *) &valid, NULL);
  return valid;
}

/* Validate a _Cilk_for construct (or a #pragma simd for loop, which
   has the same syntactic restrictions).  Returns TRUE if there were
   no errors, FALSE otherwise.  LOC is the location of the for.  DECL
   is the controlling variable.  COND is the condition.  INCRP is a
   pointer the increment expression (in case, the increment needs to
   be canonicalized).  BODY is the body of the LOOP.  */

static bool
c_check_cilk_loop (location_t loc, tree decl, tree cond, tree *incrp,
		   tree body)
{
  tree incr = *incrp;

  if (decl == error_mark_node
      || cond == error_mark_node 
      || incr == error_mark_node
      || body == error_mark_node)
    return false;

  /* Validate the initialization.  */
  gcc_assert (decl != NULL);
  if (TREE_THIS_VOLATILE (decl))
    {
      error_at (loc, "induction variable cannot be volatile");
      return false;
    }
  if (DECL_EXTERNAL (decl))
    {
      error_at (loc, "induction variable cannot be extern");
      return false;
    }
  if (TREE_STATIC (decl))
    {
      error_at (loc, "induction variable cannot be static");
      return false;
    }
  if (DECL_REGISTER (decl))
    {
      error_at (loc, "induction variable cannot be declared register");
      return false;
    }
  if (!INTEGRAL_TYPE_P (TREE_TYPE (decl))
      && !POINTER_TYPE_P (TREE_TYPE (decl)))
    {
      error_at (loc, "initialization variable must be of integral "
		"or pointer type");
      return false;
    }

  /* Validate the condition.  */
  if (!cond)
    {
      error_at (loc, "missing condition");
      return false;
    }
  bool cond_ok = false;
  if (TREE_CODE (cond) == NE_EXPR
      || TREE_CODE (cond) == LT_EXPR
      || TREE_CODE (cond) == LE_EXPR
      || TREE_CODE (cond) == GT_EXPR
      || TREE_CODE (cond) == GE_EXPR)
    {
      /* Comparison must either be:
	   DECL <comparison_operator> EXPR
	   EXPR <comparison_operator> DECL
      */
      if (decl == TREE_OPERAND (cond, 0))
	cond_ok = true;
      else if (decl == TREE_OPERAND (cond, 1))
	{
	  /* Canonicalize the comparison so the DECL is on the LHS.  */
	  TREE_SET_CODE (cond,
			 swap_tree_comparison (TREE_CODE (cond)));
	  TREE_OPERAND (cond, 1) = TREE_OPERAND (cond, 0);
	  TREE_OPERAND (cond, 0) = decl;
	  cond_ok = true;
	}
    }
  if (!cond_ok)
    {
      error_at (loc, "invalid controlling predicate");
      return false;
    }

  /* Validate and canonicalize the increment.  */
  incr = c_check_cilk_loop_incr (loc, decl, incr);
  if (incr == error_mark_node)
    return false;
  *incrp = incr;

  if (!c_check_cilk_loop_body (body))
    return false;

  return true;
}

/* Adjust any clauses to match the requirements for OpenMP.  */

static tree
adjust_clauses_for_omp (tree clauses)
{
  return clauses;
}

/* Validate and emit code for the FOR loop following a #<pragma simd>
   construct.

   LOC is the location of the location of the FOR.
   DECL is the iteration variable.
   INIT is the initialization expression.
   COND is the controlling predicate.
   INCR is the increment expression.
   BODY is the body of the loop.
   CLAUSES are the clauses associated with the pragma simd loop.

   Returns the generated statement.  */

tree
c_finish_cilk_simd_loop (location_t loc,
			 tree decl,
			 tree init, tree cond, tree incr,
			 tree body,
			 tree clauses)
{
  location_t rhs_loc;

  if (!c_check_cilk_loop (loc, decl, cond, &incr, body))
    return NULL;

  /* In the case of "for (int i = 0...)", init will be a decl.  It should
     have a DECL_INITIAL that we can turn into an assignment.  */
  if (init == decl)
    {
      rhs_loc = DECL_SOURCE_LOCATION (decl);

      init = DECL_INITIAL (decl);
      if (init == NULL)
	{
	  error_at (rhs_loc, "%qE is not initialized", decl);
	  init = integer_zero_node;
	  return NULL;
	}

      init = build_modify_expr (loc, decl, NULL_TREE, NOP_EXPR, rhs_loc,
				init, NULL_TREE);
    }

  // The C++ parser just gives us the rhs.
  if (TREE_CODE (init) != MODIFY_EXPR)
    init = build2 (MODIFY_EXPR, void_type_node, decl, init);

  gcc_assert (TREE_OPERAND (init, 0) == decl);

  tree initv = make_tree_vec (1);
  tree condv = make_tree_vec (1);
  tree incrv = make_tree_vec (1);
  TREE_VEC_ELT (initv, 0) = init;
  TREE_VEC_ELT (condv, 0) = cond;
  TREE_VEC_ELT (incrv, 0) = incr;

  tree t = make_node (CILK_SIMD);
  TREE_TYPE (t) = void_type_node;
  OMP_FOR_INIT (t) = initv;
  OMP_FOR_COND (t) = condv;
  OMP_FOR_INCR (t) = incrv;
  OMP_FOR_BODY (t) = body;
  OMP_FOR_PRE_BODY (t) = NULL;
  OMP_FOR_CLAUSES (t) = adjust_clauses_for_omp (clauses);

  SET_EXPR_LOCATION (t, loc);
  return add_stmt (t);
}

/* Validate and emit code for <#pragma simd> clauses.  */

tree
c_finish_cilk_clauses (tree clauses)
{
  for (tree c = clauses; c; c = OMP_CLAUSE_CHAIN (c))
    {
      tree prev = clauses;

      /* If a variable appears in a linear clause it cannot appear in
	 any other OMP clause.  */
      if (OMP_CLAUSE_CODE (c) == OMP_CLAUSE_LINEAR)
	for (tree c2 = clauses; c2; c2 = OMP_CLAUSE_CHAIN (c2))
	  {
	    if (c == c2)
	      continue;
	    enum omp_clause_code code = OMP_CLAUSE_CODE (c2);

	    switch (code)
	      {
	      case OMP_CLAUSE_LINEAR:
	      case OMP_CLAUSE_PRIVATE:
	      case OMP_CLAUSE_FIRSTPRIVATE:
	      case OMP_CLAUSE_LASTPRIVATE:
	      case OMP_CLAUSE_REDUCTION:
		break;

	      case OMP_CLAUSE_SAFELEN:
		goto next;

	      default:
		gcc_unreachable ();
	      }

	    if (OMP_CLAUSE_DECL (c) == OMP_CLAUSE_DECL (c2))
	      {
		error_at (OMP_CLAUSE_LOCATION (c2),
			  "variable appears in more than one clause");
		inform (OMP_CLAUSE_LOCATION (c),
			"multiple clause defined here");
		// Remove problematic clauses.
		OMP_CLAUSE_CHAIN (prev) = OMP_CLAUSE_CHAIN (c2);
	      }
	  next:
	    prev = c2;
	  }
    }

  return clauses;
}
