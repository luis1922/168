/* Gimple Represented as Polyhedra.
   Copyright (C) 2006, 2007 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <sebastian.pop@inria.fr>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* This pass converts GIMPLE to GRAPHITE, performs some loop
   transformations and then converts the resulting representation back
   to GIMPLE.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "tree.h"
#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "tree-pass.h"
#include "domwalk.h"
#include "polylib/polylibgmp.h"
#include "cloog/cloog.h"
#include "graphite.h"


VEC (scop_p, heap) *current_scops;


/* Print the schedules from SCHED.  */

void
print_graphite_bb (FILE *file, graphite_bb_p gb, int indent, int verbosity)
{
  fprintf (file, "\nGBB (\n");

  fprintf (file, "       (static schedule: ");
  print_lambda_vector (file, GBB_STATIC_SCHEDULE (gb), scop_nb_loops (GBB_SCOP (gb)) + 1);
  fprintf (file, "       )\n");

  print_loop_ir_bb (file, GBB_BB (gb), indent+2, verbosity);

  if (0)
    dump_data_references (file, GBB_DATA_REFS (gb));

  fprintf (file, ")\n");
}

/* Print SCOP to FILE.  */

static void
print_scop (FILE *file, scop_p scop, int verbosity)
{
  if (scop == NULL)
    return;

  fprintf (file, "\nSCoP_%d_%d (\n",
	   SCOP_ENTRY (scop)->index, SCOP_EXIT (scop)->index);

  fprintf (file, "       (cloog: \n");
  cloog_program_print (file, SCOP_PROG (scop));
  fprintf (file, "       )\n");

  if (scop->bbs)
    {
      graphite_bb_p gb;
      unsigned i;

      for (i = 0; VEC_iterate (graphite_bb_p, scop->bbs, i, gb); i++)
	print_graphite_bb (file, gb, 0, verbosity);
    }

  fprintf (file, ")\n");
}

/* Print all the SCOPs to FILE.  */

static void
print_scops (FILE *file, int verbosity)
{
  unsigned i;
  scop_p scop;

  for (i = 0; VEC_iterate (scop_p, current_scops, i, scop); i++)
    print_scop (file, scop, verbosity);
}

/* Debug SCOP.  */

void
debug_scop (scop_p scop, int verbosity)
{
  print_scop (stderr, scop, verbosity);
}

/* Debug all SCOPs from CURRENT_SCOPS.  */

void 
debug_scops (int verbosity)
{
  print_scops (stderr, verbosity);
}



/* Return true when EXPR is an affine function in LOOP.  */

static bool
affine_expr (struct loop *loop, tree expr)
{
  tree scev = analyze_scalar_evolution (loop, expr);

  scev = instantiate_parameters (loop, scev);

  return (evolution_function_is_affine_multivariate_p (scev)
	  || evolution_function_is_constant_p (scev));
}


/* Return true only when STMT is simple enough for being handled by
   GRAPHITE.  */

static bool
stmt_simple_for_scop_p (tree stmt)
{
  struct loop *loop = bb_for_stmt (stmt)->loop_father;

  /* ASM_EXPR and CALL_EXPR may embed arbitrary side effects.
     Calls have side-effects, except those to const or pure
     functions.  */
  if ((TREE_CODE (stmt) == CALL_EXPR
       && !(call_expr_flags (stmt) & (ECF_CONST | ECF_PURE)))
      || (TREE_CODE (stmt) == ASM_EXPR
	  && ASM_VOLATILE_P (stmt)))
    return false;

  switch (TREE_CODE (stmt))
    {
    case LABEL_EXPR:
      return true;

    case COND_EXPR:
      {
	tree opnd0 = TREE_OPERAND (stmt, 0);

	switch (TREE_CODE (opnd0))
	  {
	  case NE_EXPR:
	  case EQ_EXPR:
	  case LT_EXPR:
	  case GT_EXPR:
	  case LE_EXPR:
	  case GE_EXPR:
	    return (affine_expr (loop, TREE_OPERAND (opnd0, 0)) 
		    && affine_expr (loop, TREE_OPERAND (opnd0, 1)));
	  default:
	    return false;
	  }
      }

    case GIMPLE_MODIFY_STMT:
      {
	tree opnd0 = GIMPLE_STMT_OPERAND (stmt, 0);
	tree opnd1 = GIMPLE_STMT_OPERAND (stmt, 1);

	if (TREE_CODE (opnd0) == ARRAY_REF 
	     || TREE_CODE (opnd0) == INDIRECT_REF
	     || TREE_CODE (opnd0) == COMPONENT_REF)
	  {
	    if (!create_data_ref (opnd0, stmt, false))
	      return false;

	    if (TREE_CODE (opnd1) == ARRAY_REF 
		|| TREE_CODE (opnd1) == INDIRECT_REF
		|| TREE_CODE (opnd1) == COMPONENT_REF)
	      {
		if (!create_data_ref (opnd1, stmt, true))
		  return false;

		return true;
	      }
	  }

	if (TREE_CODE (opnd1) == ARRAY_REF 
	     || TREE_CODE (opnd1) == INDIRECT_REF
	     || TREE_CODE (opnd1) == COMPONENT_REF)
	  {
	    if (!create_data_ref (opnd1, stmt, true))
	      return false;

	    return true;
	  }

	/* We cannot return (affine_expr (loop, opnd0) &&
	   affine_expr (loop, opnd1)) because D.1882_16 is not affine
	   in the following:

	   D.1881_15 = a[j_13][pretmp.22_20];
	   D.1882_16 = D.1881_15 + 2;
	   a[j_22][i_12] = D.1882_16;

	   but this is valid code in a scop.

	   FIXME: I'm not yet 100% sure that returning true is safe:
	   there might be exponential scevs.  On the other hand, if
	   these exponential scevs do not reference arrays, then
	   access functions, domains and schedules remain affine.  So
	   it is very well possible that we can handle exponential
	   scalar computations.  */
	return true;
      }

    case CALL_EXPR:
      {
	tree args;

	for (args = TREE_OPERAND (stmt, 1); args; args = TREE_CHAIN (args))
	  if ((TREE_CODE (TREE_VALUE (args)) == ARRAY_REF
	       || TREE_CODE (TREE_VALUE (args)) == INDIRECT_REF
	       || TREE_CODE (TREE_VALUE (args)) == COMPONENT_REF)
	      && !create_data_ref (TREE_VALUE (args), stmt, true))
	    return false;

	return true;
      }

    case RETURN_EXPR:
    default:
      /* These nodes cut a new scope.  */
      return false;
    }

  return false;
}

/* This function verify if a basic block contains simple statements,
   returns true if a BB contains only simple statements.  */

static bool
basic_block_simple_for_scop_p (basic_block bb)
{
  block_stmt_iterator bsi;

  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    if (!stmt_simple_for_scop_p (bsi_stmt (bsi)))
      return false;

  return true;
}

/* Return true when BB is contained in SCOP.  */

static inline bool
bb_in_scop_p (basic_block bb, scop_p scop)
{
  return (dominated_by_p (CDI_DOMINATORS, bb, SCOP_ENTRY (scop))
	  && dominated_by_p (CDI_POST_DOMINATORS, bb, SCOP_EXIT (scop)));
}

/* Return true when STMT is contained in SCOP.  */

static inline bool
stmt_in_scop_p (tree stmt, scop_p scop)
{
  return bb_in_scop_p (bb_for_stmt (stmt), scop);
}

static inline bool
function_parameter_p (tree var)
{
  return (TREE_CODE (SSA_NAME_VAR (var)) == PARM_DECL);
}

/* Return true when VAR is invariant in SCOP.  In that case, VAR is a
   parameter of SCOP.  */

static inline bool
invariant_in_scop_p (tree var, scop_p scop)
{
  gcc_assert (TREE_CODE (var) == SSA_NAME);

  return (function_parameter_p (var)
	  || !stmt_in_scop_p (SSA_NAME_DEF_STMT (var), scop));
}

/* Find the first basic block that dominates BB and that exits the
   current loop.  */

static basic_block
get_loop_start (basic_block bb)
{
  basic_block res = bb;
  int depth;

  do {
    res = get_immediate_dominator (CDI_DOMINATORS, res);
    depth = res->loop_father->depth;
  } while (depth != 0 && depth >= bb->loop_father->depth);

  return res;
}

static scop_p down_open_scop;

/* Creates a new scop starting with BB.  */

static scop_p
new_scop (basic_block bb)
{
  scop_p scop = XNEW (struct scop);

  SCOP_ENTRY (scop) = bb;
  SCOP_BBS (scop) = VEC_alloc (graphite_bb_p, heap, 3);
  SCOP_LOOP_NEST (scop) = VEC_alloc (loop_p, heap, 3);
  SCOP_PARAMS (scop) = VEC_alloc (tree, heap, 3);
  SCOP_PROG (scop) = cloog_program_malloc ();
  SCOP_PROG (scop)->names = cloog_names_malloc ();

  return scop;
}

/* Save the SCOP.  */

static inline void
save_scop (scop_p scop)
{
  VEC_safe_push (scop_p, heap, current_scops, scop);
}

/* Build the SCoP ending with BB.  */

static void
end_scop (basic_block bb)
{
  scop_p scop;
  basic_block loop_start = get_loop_start (bb);

  if (dominated_by_p (CDI_DOMINATORS, SCOP_ENTRY (down_open_scop), loop_start))
    {
      scop = down_open_scop;
      SCOP_EXIT (scop) = bb;
    }
  else
    {
      scop = new_scop (loop_start);
      SCOP_EXIT (scop) = bb;
      end_scop (loop_start);
    }

  save_scop (scop);
}

/* Mark difficult constructs as boundaries of SCoPs.  Callback for
   walk_dominator_tree.  */

static void
test_for_scop_bound (struct dom_walk_data *dw_data ATTRIBUTE_UNUSED,
		     basic_block bb)
{
  if (bb == ENTRY_BLOCK_PTR)
    return;

  if (debug_p ())
    fprintf (dump_file, "down bb_%d\n", bb->index);

  /* Exiting the loop containing the open scop ends the scop.  */
  if (SCOP_ENTRY (down_open_scop)->loop_father->depth > bb->loop_father->depth)
    {
      SCOP_EXIT (down_open_scop) = bb;
      save_scop (down_open_scop);
      down_open_scop = new_scop (bb);

      if (debug_p ())
	fprintf (dump_file, "dom bound bb_%d\n\n", bb->index);

      return;
    }

  if (!basic_block_simple_for_scop_p (bb))
    {
      end_scop (bb);
      down_open_scop = new_scop (bb);

      if (debug_p ())
	fprintf (dump_file, "difficult bound bb_%d\n\n", bb->index);

      return;
    }
}

/* Find static control parts.  */

static void
build_scops (void)
{
  struct dom_walk_data walk_data;

  down_open_scop = new_scop (ENTRY_BLOCK_PTR);
  memset (&walk_data, 0, sizeof (struct dom_walk_data));
  walk_data.before_dom_children_before_stmts = test_for_scop_bound;

  init_walk_dominator_tree (&walk_data);
  walk_dominator_tree (&walk_data, ENTRY_BLOCK_PTR);
  fini_walk_dominator_tree (&walk_data);

  /* End the last open scop.  */
  SCOP_EXIT (down_open_scop) = EXIT_BLOCK_PTR;
  save_scop (down_open_scop);
}



/* Get the index corresponding to VAR in the current LOOP.  If
   it's the first time we ask for this VAR, then we return
   chrec_not_analyzed_yet for this VAR and return its index.  */

static int
param_index (tree var, scop_p scop)
{
  int i;
  tree p;

  gcc_assert (TREE_CODE (var) == SSA_NAME);

  for (i = 0; VEC_iterate (tree, SCOP_PARAMS (scop), i, p); i++)
    if (p == var)
      return i;

  VEC_safe_push (tree, heap, SCOP_PARAMS (scop), var);
  return VEC_length (tree, SCOP_PARAMS (scop)) - 1;
}

/* Build for BB the static schedule.  */

static void
build_graphite_bb (struct dom_walk_data *dw_data, basic_block bb)
{
  struct graphite_bb *gb;
  scop_p scop = (scop_p) dw_data->global_data;

  /* Scop's exit is not in the scop.  */
  if (bb == SCOP_EXIT (scop)
      /* Every block in the scop dominates scop's exit.  */
      || !dominated_by_p (CDI_DOMINATORS, SCOP_EXIT (scop), bb))
    return;

  /* Build the new representation for the basic block.  */
  gb = XNEW (struct graphite_bb);
  GBB_BB (gb) = bb;
  GBB_SCOP (gb) = scop;

  /* Store the GRAPHITE representation of the current BB.  */
  VEC_safe_push (graphite_bb_p, heap, scop->bbs, gb);
}

/* Gather the basic blocks belonging to the SCOP.  */

static void
build_scop_bbs (scop_p scop)
{
  struct dom_walk_data walk_data;

  memset (&walk_data, 0, sizeof (struct dom_walk_data));

  /* Iterate over all the basic blocks of the scop in their pseudo
     execution order, and associate to each bb a static schedule.
     (pseudo exec order = the branches of a condition are scheduled
     sequentially: the then clause comes before the else clause.)  */
  walk_data.before_dom_children_before_stmts = build_graphite_bb;

  walk_data.global_data = scop;
  init_walk_dominator_tree (&walk_data);
  walk_dominator_tree (&walk_data, SCOP_ENTRY (scop));
  fini_walk_dominator_tree (&walk_data);
}

/* Returns true when LOOP is in SCOP.  */

static bool 
loop_in_scop_p (struct loop *loop, scop_p scop)
{
  unsigned i;
  struct loop *l;

  for (i = 0; VEC_iterate (loop_p, SCOP_LOOP_NEST (scop), i, l); i++)
    if (l == loop)
      return true;

  return false;
}

/* Record LOOP as occuring in SCOP.  */

static void
scop_record_loop (scop_p scop, struct loop *loop)
{
  if (loop_in_scop_p (loop, scop))
    return;

  VEC_safe_push (loop_p, heap, SCOP_LOOP_NEST (scop), loop);
}

/* Build the loop nests contained in SCOP.  */

static void
build_scop_loop_nests (scop_p scop)
{
  unsigned i;
  graphite_bb_p gb;

  for (i = 0; VEC_iterate (graphite_bb_p, SCOP_BBS (scop), i, gb); i++)
    scop_record_loop (scop, GBB_LOOP (gb));
}

/* Build for BB the static schedule.  */

static void
build_scop_canonical_schedules (scop_p scop)
{
  unsigned i;
  graphite_bb_p gb;
  unsigned nb = scop_nb_loops (scop) + 1;

  SCOP_STATIC_SCHEDULE (scop) = lambda_vector_new (nb);

  for (i = 0; VEC_iterate (graphite_bb_p, SCOP_BBS (scop), i, gb); i++)
    {
      SCOP_STATIC_SCHEDULE (scop)[scop_loop_index (scop, GBB_LOOP (gb))] += 1;
      GBB_STATIC_SCHEDULE (gb) = lambda_vector_new (nb);
      lambda_vector_copy (SCOP_STATIC_SCHEDULE (scop), 
			  GBB_STATIC_SCHEDULE (gb), nb);
    }
}

struct irp_data
{
  struct loop *loop;
  scop_p scop;
};

/* Record VAR as a parameter of DTA->scop.  */

static bool
idx_record_param (tree *var, void *dta)
{
  struct irp_data *data = dta;

  switch (TREE_CODE (*var))
    {
    case SSA_NAME:
      if (invariant_in_scop_p (*var, data->scop))
	param_index (*var, data->scop);
      return true;

    default:
      return true;
    }
}

/* Record parameters occurring in an evolution function of a data
   access, niter expression, etc.  Callback for for_each_index.  */

static bool
idx_record_params (tree base, tree *idx, void *dta)
{
  struct irp_data *data = dta;

  if (TREE_CODE (base) != ARRAY_REF)
    return true;

  if (TREE_CODE (*idx) == SSA_NAME)
    {
      tree scev;
      scop_p scop = data->scop;
      struct loop *loop = data->loop;
      struct loop *floop = SCOP_ENTRY (scop)->loop_father;

      scev = analyze_scalar_evolution (loop, *idx);
      scev = instantiate_parameters (floop, scev);
      for_each_scev_op (&scev, idx_record_param, dta);
    }

  return true;
}

/* Helper function for walking in dominance order basic blocks.  Find
   parameters with respect to SCOP in memory access functions used in
   BB.  DW_DATA contains the context and the results for extract
   parameters information.  */

static void
find_params_in_bb (struct dom_walk_data *dw_data, basic_block bb)
{
  unsigned i;
  data_reference_p dr;
  VEC (data_reference_p, heap) *drs;
  block_stmt_iterator bsi;
  scop_p scop = (scop_p) dw_data->global_data;

  /* Find the parameters used in the memory access functions.  */
  drs = VEC_alloc (data_reference_p, heap, 5);
  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    find_data_references_in_stmt (bsi_stmt (bsi), &drs);

  for (i = 0; VEC_iterate (data_reference_p, drs, i, dr); i++)
    {
      struct irp_data irp;

      irp.loop = bb->loop_father;
      irp.scop = scop;
      for_each_index (&dr->ref, idx_record_params, &irp);
    }

  VEC_free (data_reference_p, heap, drs);
}

/* Initialize Cloog's parameter names from the names used in GIMPLE.  */

static void
initialize_cloog_names (scop_p scop)
{
  unsigned i, nb_params = VEC_length (tree, SCOP_PARAMS (scop));
  char **params = XNEWVEC (char *, nb_params);
  tree p;

  for (i = 0; VEC_iterate (tree, SCOP_PARAMS (scop), i, p); i++)
    {
      const char *name = get_name (SSA_NAME_VAR (p));

      if (name)
	{
	  params[i] = XNEWVEC (char, strlen (name) + 12);
	  sprintf (params[i], "%s_%d", name, SSA_NAME_VERSION (p));
	}
      else
	{
	  params[i] = XNEWVEC (char, 12);
	  sprintf (params[i], "T_%d", SSA_NAME_VERSION (p));
	}
    }

  SCOP_PROG (scop)->names->nb_parameters = nb_params;
  SCOP_PROG (scop)->names->parameters = params;
}

/* Record the parameters used in the SCOP.  A variable is a parameter
   in a scop if it does not vary during the execution of that scop.  */

static void
find_scop_parameters (scop_p scop)
{
  unsigned i;
  struct dom_walk_data walk_data;
  struct loop *loop;

  /* Find the parameters used in the loop bounds.  */
  for (i = 0; VEC_iterate (loop_p, SCOP_LOOP_NEST (scop), i, loop); i++)
    {
      tree nb_iters = number_of_latch_executions (loop);

      if (chrec_contains_symbols (nb_iters))
	{
	  struct irp_data irp;

	  irp.loop = loop;
	  irp.scop = scop;
	  for_each_scev_op (&nb_iters, idx_record_param, &irp);
	}
    }

  /* Find the parameters used in data accesses.  */
  memset (&walk_data, 0, sizeof (struct dom_walk_data));
  walk_data.before_dom_children_before_stmts = find_params_in_bb;
  walk_data.global_data = scop;
  init_walk_dominator_tree (&walk_data);
  walk_dominator_tree (&walk_data, SCOP_ENTRY (scop));
  fini_walk_dominator_tree (&walk_data);

  initialize_cloog_names (scop);
}

/* Returns the number of parameters for SCOP.  */

static inline unsigned
nb_params_in_scop (scop_p scop)
{
  return VEC_length (tree, SCOP_PARAMS (scop));
}

/* Build the context constraints for SCOP: constraints and relations
   on parameters.  */

static void
build_scop_context (scop_p scop)
{
  unsigned nb_params = nb_params_in_scop (scop);
  CloogMatrix *matrix = cloog_matrix_alloc (1, nb_params + 2);

  value_init (matrix->p[0][0]);
  value_set_si (matrix->p[0][0], 1);

  value_init (matrix->p[0][nb_params + 1]);
  value_set_si (matrix->p[0][nb_params + 1], -1);

  SCOP_PROG (scop)->context = cloog_domain_matrix2domain (matrix);
}

/* Scan EXPR and translate it to an inequality vector INEQ that will
   be inserted in the domain matrix.  */

static void
scan_tree_for_params (scop_p scop, tree expr, CloogMatrix *cstr, int row,
		      Value k)
{
  int col;

  switch (TREE_CODE (expr))
    {
    case MULT_EXPR:
      if (chrec_contains_symbols (TREE_OPERAND (expr, 0)))
	{
	  Value val;

	  gcc_assert (host_integerp (TREE_OPERAND (expr, 1), 1));

	  value_init (val);
	  value_set_si (val, int_cst_value (TREE_OPERAND (expr, 1)));
	  value_multiply (k, k, val);
	  value_clear (val);
	  scan_tree_for_params (scop, TREE_OPERAND (expr, 0), cstr, row, k);
	}
      else
	{
	  Value val;

	  gcc_assert (host_integerp (TREE_OPERAND (expr, 0), 1));

	  value_init (val);
	  value_set_si (val, int_cst_value (TREE_OPERAND (expr, 0)));
	  value_multiply (k, k, val);
	  value_clear (val);
	  scan_tree_for_params (scop, TREE_OPERAND (expr, 1), cstr, row, k);
	}
      break;

    case PLUS_EXPR:
      scan_tree_for_params (scop, TREE_OPERAND (expr, 0), cstr, row, k);
      scan_tree_for_params (scop, TREE_OPERAND (expr, 1), cstr, row, k);
      break;

    case MINUS_EXPR:
      scan_tree_for_params (scop, TREE_OPERAND (expr, 0), cstr, row, k);
      value_oppose (k, k);
      scan_tree_for_params (scop, TREE_OPERAND (expr, 1), cstr, row, k);
      break;

    case SSA_NAME:
      col = scop_nb_loops (scop) + param_index (expr, scop);
      value_init (cstr->p[row][col]);
      value_assign (cstr->p[row][col], k);
      break;

    case INTEGER_CST:
      col = scop_nb_loops (scop) + scop_nb_params (scop);
      value_init (cstr->p[row][col]);
      value_set_si (cstr->p[row][col], int_cst_value (expr));
      break;

    case NOP_EXPR:
    case CONVERT_EXPR:
    case NON_LVALUE_EXPR:
      scan_tree_for_params (scop, TREE_OPERAND (expr, 0), cstr, row, k);
      break;

    default:
      break;
    }
}

/* Returns the first loop in SCOP, returns NULL if there is no loop in
   SCOP.  */

static struct loop *
first_loop_in_scop (scop_p scop)
{
  struct loop *loop = SCOP_ENTRY (scop)->loop_father->inner;

  if (loop == NULL)
    return NULL;

  while (loop->next && !loop_in_scop_p (loop, scop))
    loop = loop->next;

  if (loop_in_scop_p (loop, scop))
    return loop;

  return NULL;
}

/* Converts LOOP in SCOP to cloog's format.  NB_ITERATORS is the
   number of loops surrounding LOOP in SCOP.  OUTER_CSTR gives the
   constraints matrix for the surrounding loops.  */

static CloogLoop *
setup_cloog_loop (scop_p scop, struct loop *loop, CloogMatrix *outer_cstr,
		  int nb_iterators)
{
  unsigned i, j, row;
  unsigned nb_rows = outer_cstr->NbRows + 1;
  unsigned nb_cols = outer_cstr->NbColumns;
  CloogMatrix *cstr;
  CloogStatement *statement;
  CloogLoop *res = cloog_loop_malloc ();
  tree nb_iters = number_of_latch_executions (loop);

  if (TREE_CODE (nb_iters) == INTEGER_CST
      || !chrec_contains_undetermined (nb_iters))
    nb_rows++;

  cstr = cloog_matrix_alloc (nb_rows, nb_cols);

  for (i = 0; i < outer_cstr->NbRows; i++)
    for (j = 0; j < outer_cstr->NbColumns; j++)
      {
	value_init (cstr->p[i][j]);
	value_assign (cstr->p[i][j], outer_cstr->p[i][j]);
      }

  /* 0 <= loop_i */
  row = outer_cstr->NbRows;
  value_init (cstr->p[row][row]);
  value_set_si (cstr->p[row][row], 1);

  /* loop_i <= nb_iters */
  row++;

  if (TREE_CODE (nb_iters) == INTEGER_CST)
    {
      value_init (cstr->p[row][row]);
      value_set_si (cstr->p[row][row], -1);

      value_init (cstr->p[row][scop_dim_domain (scop) - 1]);
      value_set_si (cstr->p[row][scop_dim_domain (scop) - 1],
		    int_cst_value (nb_iters));
    }
  else if (!chrec_contains_undetermined (nb_iters))
    {
      /* Otherwise nb_iters contains parameters: scan the nb_iters
	 expression and build its matrix representation.  */
      Value one;

      nb_iters = instantiate_parameters (SCOP_ENTRY (scop)->loop_father,
					 nb_iters);
      value_init (one);
      value_set_si (one, 1);
      scan_tree_for_params (scop, nb_iters, cstr, row, one);
      value_clear (one);
    }

  res->domain = cloog_domain_matrix2domain (cstr);

  if (loop->inner && loop_in_scop_p (loop->inner, scop))
    res->inner = setup_cloog_loop (scop, loop->inner, cstr, nb_iterators + 1);

  if (loop->next && loop_in_scop_p (loop->next, scop))
    res->next = setup_cloog_loop (scop, loop->next, outer_cstr, nb_iterators);

  {
    static int number = 0;
    statement = cloog_statement_alloc (number++);
  }
  res->block = cloog_block_alloc (statement, NULL, 0, NULL, nb_iterators + 1);
  
  return res;
}

/* Build the current domain matrix: the loops belonging to the current
   SCOP, and that vary for the execution of the current basic block.
   Returns false if there is no loop in SCOP.  */

static bool
build_scop_iteration_domain (scop_p scop)
{
  struct loop *loop = first_loop_in_scop (scop);
  CloogMatrix *outer_cstr;

  if (loop == NULL)
    return false;

  outer_cstr = cloog_matrix_alloc (0, scop_dim_domain (scop));
  SCOP_PROG (scop)->loop = setup_cloog_loop (scop, loop, outer_cstr, 0);
  return true;
}

/* Initializes an equation CY of the access matrix using the
   information for a subscript from ACCESS_FUN, relatively to the loop
   indexes from LOOP_NEST and parameter indexes from PARAMS.  Returns
   true when the operation succeeded.  */

static bool
build_access_matrix_with_af (tree access_fun, lambda_vector cy,
			     scop_p scop)
{
  VEC (loop_p, heap) *loop_nest = SCOP_LOOP_NEST (scop);
  VEC (tree, heap) *params = SCOP_PARAMS (scop);

  switch (TREE_CODE (access_fun))
    {
    case POLYNOMIAL_CHREC:
      {
	tree left = CHREC_LEFT (access_fun);
	tree right = CHREC_RIGHT (access_fun);
	int var = CHREC_VARIABLE (access_fun);
	unsigned var_idx;
	struct loop *loopi;

	if (TREE_CODE (right) != INTEGER_CST)
	  return false;

	/* Find the index of the current variable VAR_IDX in the
	   LOOP_NEST array.  */
	for (var_idx = 0; VEC_iterate (loop_p, loop_nest, var_idx, loopi);
	     var_idx++)
	  if (loopi->num == var)
	    break;

	gcc_assert (loopi && loopi->num == var);

	cy[var_idx] = int_cst_value (right);

	switch (TREE_CODE (left))
	  {
	  case POLYNOMIAL_CHREC:
	    return build_access_matrix_with_af (left, cy, scop);

	  case INTEGER_CST:
	    {
	      /* Constant part.  */
	      unsigned nb_loops = VEC_length (loop_p, loop_nest);
	      unsigned nb_params = VEC_length (tree, params);

	      cy[nb_loops + nb_params] = int_cst_value (left);
	      return true;
	    }

	  default:
	    return false;
	  }
      }
    case INTEGER_CST:
      {
	/* Constant part.  */
	unsigned nb_loops = VEC_length (loop_p, loop_nest);
	unsigned nb_params = VEC_length (tree, params);
	cy[nb_loops + nb_params] = int_cst_value (access_fun);
	return true;
      }

    default:
      return false;
    }
}

/* Initialize the access matrix in the data reference REF with respect
   to the loop nesting LOOP_NEST.  Return true when the operation
   succeeded.  */

static bool
build_access_matrix (data_reference_p ref, graphite_bb_p gb)
{
  unsigned i, ndim = DR_NUM_DIMENSIONS (ref);

  DR_SCOP (ref) = GBB_SCOP (gb);
  DR_ACCESS_MATRIX (ref) = VEC_alloc (lambda_vector, heap, ndim);

  for (i = 0; i < ndim; i++)
    {
      lambda_vector v = lambda_vector_new (gbb_dim_domain (gb));
      scop_p scop = GBB_SCOP (gb);
      tree af = DR_ACCESS_FN (ref, i);

      if (!build_access_matrix_with_af (af, v, scop))
	return false;

      VEC_safe_push (lambda_vector, heap, DR_ACCESS_MATRIX (ref), v);
    }

  return true;
}

/* Build a schedule for each basic-block in the SCOP.  */

static void
build_scop_data_accesses (scop_p scop)
{
  unsigned i;
  graphite_bb_p gb;

  for (i = 0; VEC_iterate (graphite_bb_p, SCOP_BBS (scop), i, gb); i++)
    {
      unsigned j;
      block_stmt_iterator bsi;
      data_reference_p dr;

      /* On each statement of the basic block, gather all the occurences
	 to read/write memory.  */
      GBB_DATA_REFS (gb) = VEC_alloc (data_reference_p, heap, 5);
      for (bsi = bsi_start (GBB_BB (gb)); !bsi_end_p (bsi); bsi_next (&bsi))
	find_data_references_in_stmt (bsi_stmt (bsi), &GBB_DATA_REFS (gb));

      /* Construct the access matrix for each data ref, with respect to
	 the loop nest of the current BB in the considered SCOP.  */
      for (j = 0; VEC_iterate (data_reference_p, GBB_DATA_REFS (gb), j, dr); j++)
	build_access_matrix (dr, gb);
    }
}

/* Find the right transform for the SCOP, and return a Cloog AST
   representing the new form of the program.  */

static struct clast_stmt *
find_transform (scop_p scop ATTRIBUTE_UNUSED)
{
  CloogOptions *options = cloog_options_malloc ();
  CloogProgram *prog = cloog_program_generate (SCOP_PROG (scop), options);
  struct clast_stmt *stmt = cloog_clast_create (prog, options);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      pprint (dump_file, stmt, 0, options);
      cloog_program_dump_cloog (dump_file, prog);
    }

  return stmt;
}

/* GIMPLE Loop Generator: generates loops from STMT in GIMPLE form for
   the given SCOP.  */

static void
gloog (scop_p scop ATTRIBUTE_UNUSED, struct clast_stmt *stmt)
{
  cloog_clast_free (stmt);
}


/* Pretty print a scop in DOT format.  */

static void 
dot_scop_1 (FILE *file, scop_p scop)
{
  edge e;
  edge_iterator ei;
  basic_block bb;
  basic_block entry = SCOP_ENTRY (scop);
  basic_block exit = SCOP_EXIT (scop);
    
  fprintf (file, "digraph SCoP_%d_%d {\n", entry->index,
	   exit->index);

  FOR_ALL_BB (bb)
    {
      if (bb == entry)
	fprintf (file, "%d [shape=triangle];\n", bb->index);

      if (bb == exit)
	fprintf (file, "%d [shape=box];\n", bb->index);

      if (bb_in_scop_p (bb, scop)) 
	fprintf (file, "%d [color=red];\n", bb->index);

      FOR_EACH_EDGE (e, ei, bb->succs)
	fprintf (file, "%d -> %d;\n", bb->index, e->dest->index);
    }

  fputs ("}\n\n", file);
}

/* Display SCOP using dotty.  */

void
dot_scop (scop_p scop)
{
  FILE *stream = fopen ("/tmp/foo.dot", "w");
  gcc_assert (stream != NULL);

  dot_scop_1 (stream, scop);
  fclose (stream);

  system ("dotty /tmp/foo.dot");
}


/* Perform a set of linear transforms on LOOPS.  */

void
graphite_transform_loops (void)
{
  unsigned i;
  scop_p scop;

  current_scops = VEC_alloc (scop_p, heap, 3);

  calculate_dominance_info (CDI_POST_DOMINATORS);
  calculate_dominance_info (CDI_DOMINATORS);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Graphite loop transformations \n");

  build_scops ();

  for (i = 0; VEC_iterate (scop_p, current_scops, i, scop); i++)
    {
      build_scop_bbs (scop);
      build_scop_loop_nests (scop);
      build_scop_canonical_schedules (scop);
      find_scop_parameters (scop);
      build_scop_context (scop);

      if (!build_scop_iteration_domain (scop))
	continue;

      build_scop_data_accesses (scop);
      gloog (scop, find_transform (scop));
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      print_scops (dump_file, 2);
      fprintf (dump_file, "\nnumber of SCoPs: %d\n",
	       VEC_length (scop_p, current_scops));
    }
}
