/* Generate the machine mode enumeration and associated tables.
   Copyright (C) 2003
   Free Software Foundation, Inc.

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
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

#include "bconfig.h"
#include "system.h"
#include "errors.h"

/* enum mode_class is normally defined by machmode.h but we can't
   include that header here.  */
#include "mode-classes.def"

#define DEF_MODE_CLASS(M) M
enum mode_class { MODE_CLASSES, MAX_MODE_CLASS };
#undef DEF_MODE_CLASS

/* Text names of mode classes, for output.  */
#define DEF_MODE_CLASS(M) #M
static const char *const mode_class_names[MAX_MODE_CLASS] =
{
  MODE_CLASSES
};
#undef DEF_MODE_CLASS
#undef MODE_CLASSES

#ifdef EXTRA_MODES_FILE
# define HAVE_EXTRA_MODES 1
#else
# define HAVE_EXTRA_MODES 0
# define EXTRA_MODES_FILE ""
#endif

/* Data structure for building up what we know about a mode.
   They're clustered by mode class.  */
struct mode_data
{
  struct mode_data *next;	/* next this class - arbitrary order */

  const char *name;		/* printable mode name -- SI, not SImode */
  enum mode_class class;	/* this mode class */
  unsigned int bitsize;		/* size in bits, equiv to TYPE_PRECISION */
  unsigned int bytesize;	/* storage size in addressable units */
  unsigned int ncomponents;	/* number of subunits */
  unsigned int alignment;	/* mode alignment */
  const char *format;		/* floating point format - MODE_FLOAT only */

  struct mode_data *component;	/* mode of components */
  struct mode_data *wider;	/* next wider mode */

  const char *file;		/* file and line of definition, */
  unsigned int line;		/* for error reporting */
};

static struct mode_data *known_modes[MAX_MODE_CLASS];
static unsigned int n_modes[MAX_MODE_CLASS];
static struct mode_data *void_mode;

static const struct mode_data blank_mode = {
  0, "<unknown>", MAX_MODE_CLASS,
  -1, -1, -1, -1,
  0, 0, 0,
  "<unknown>", 0
};

/* Mode class operations.  */
static enum mode_class
complex_class (enum mode_class class)
{
  switch (class)
    {
    case MODE_INT: return MODE_COMPLEX_INT;
    case MODE_FLOAT: return MODE_COMPLEX_FLOAT;
    default:
      error ("no complex class for class %s", mode_class_names[class]);
      return MODE_RANDOM;
    }
}

static enum mode_class
vector_class (enum mode_class class)
{
  switch (class)
    {
    case MODE_INT: return MODE_VECTOR_INT;
    case MODE_FLOAT: return MODE_VECTOR_FLOAT;
    default:
      error ("no vector class for class %s", mode_class_names[class]);
      return MODE_RANDOM;
    }
}

static struct mode_data *
find_mode (enum mode_class class, const char *name)
{
  struct mode_data *m;

  for (m = known_modes[class]; m; m = m->next)
    if (!strcmp (name, m->name))
      return m;

  return 0;
}

static struct mode_data *
new_mode (enum mode_class class, const char *name,
	  const char *file, unsigned int line)
{
  struct mode_data *m;

  m = find_mode (class, name);
  if (m)
    {
      error ("%s:%d: duplicate definition of mode \"%s\"",
	     trim_filename (file), line, name);
      error ("%s:%d: previous definition here", m->file, m->line);
      return m;
    }

  m = xmalloc (sizeof (struct mode_data));
  memcpy (m, &blank_mode, sizeof (struct mode_data));
  m->class = class;
  m->name = name;
  if (file)
    m->file = trim_filename (file);
  m->line = line;

  m->next = known_modes[class];
  known_modes[class] = m;
  n_modes[class]++;
  return m;
}

#define for_all_modes(C, M)			\
  for (C = 0; C < MAX_MODE_CLASS; C++)		\
    for (M = known_modes[C]; M; M = M->next)


/* Diagnose failure to meet expectations in a partially filled out
   mode structure.  */
enum requirement { SET, UNSET, OPTIONAL };

#define validate_field_(mname, fname, req, val, unset, file, line) do {	\
  switch (req)								\
    {									\
    case SET:								\
      if (val == unset)							\
	error ("%s:%d: (%s) field %s must be set",			\
	       file, line, mname, fname);				\
      break;								\
    case UNSET:								\
      if (val != unset)							\
	error ("%s:%d: (%s) field %s must not be set",			\
	       file, line, mname, fname);				\
    case OPTIONAL:							\
      break;								\
    }									\
} while (0)

#define validate_field(M, F) \
  validate_field_(M->name, #F, r_##F, M->F, blank_mode.F, M->file, M->line)

static void
validate_mode (struct mode_data *m,
	       enum requirement r_bitsize,
	       enum requirement r_bytesize,
	       enum requirement r_component,
	       enum requirement r_ncomponents,
	       enum requirement r_format)
{
  validate_field (m, bitsize);
  validate_field (m, bytesize);
  validate_field (m, component);
  validate_field (m, ncomponents);
  validate_field (m, format);
}
#undef validate_field
#undef validate_field_

/* Given a partially-filled-out mode structure, figure out what we can
   and fill the rest of it in; die if it isn't enough.  */
static void
complete_mode (struct mode_data *m)
{
  unsigned int alignment;

  if (!m->name)
    {
      error ("%s:%d: mode with no name", m->file, m->line);
      return;
    }
  if (m->class == MAX_MODE_CLASS)
    {
      error ("%s:%d: %smode has no mode class", m->file, m->line, m->name);
      return;
    }

  switch (m->class)
    {
    case MODE_RANDOM:
      /* Nothing more need be said.  */
      if (!strcmp (m->name, "VOID"))
	void_mode = m;

      validate_mode (m, UNSET, UNSET, UNSET, UNSET, UNSET);

      m->bitsize = 0;
      m->bytesize = 0;
      m->ncomponents = 0;
      m->component = 0;
      break;

    case MODE_CC:
      /* Again, nothing more need be said.  For historical reasons,
	 the size of a CC mode is four units.  */
      validate_mode (m, UNSET, UNSET, UNSET, UNSET, UNSET);

      m->bytesize = 4;
      m->ncomponents = 0;
      m->component = 0;
      break;

    case MODE_INT:
    case MODE_FLOAT:
      /* A scalar mode must have a byte size, may have a bit size,
	 and must not have components.   A float mode must have a
         format.  */
      validate_mode (m, OPTIONAL, SET, UNSET, UNSET,
		     m->class == MODE_FLOAT ? SET : UNSET);

      m->ncomponents = 0;
      m->component = 0;
      break;

    case MODE_PARTIAL_INT:
      /* A partial integer mode uses ->component to say what the
	 corresponding full-size integer mode is, and may also
	 specify a bit size.  */
      validate_mode (m, OPTIONAL, UNSET, SET, UNSET, UNSET);

      m->bytesize = m->component->bytesize;

      m->ncomponents = 0;
      m->component = 0;  /* ??? preserve this */
      break;

    case MODE_COMPLEX_INT:
    case MODE_COMPLEX_FLOAT:
      /* Complex modes should have a component indicated, but no more.  */
      validate_mode (m, UNSET, UNSET, SET, UNSET, UNSET);
      m->ncomponents = 2;
      if (m->component->bitsize != (unsigned int)-1)
	m->bitsize = 2 * m->component->bitsize;
      m->bytesize = 2 * m->component->bytesize;
      break;

    case MODE_VECTOR_INT:
    case MODE_VECTOR_FLOAT:
      /* Vector modes should have a component and a number of components.  */
      validate_mode (m, UNSET, UNSET, SET, SET, UNSET);
      if (m->component->bitsize != (unsigned int)-1)
	m->bitsize = m->ncomponents * m->component->bitsize;
      m->bytesize = m->ncomponents * m->component->bytesize;
      break;

    default:
      abort ();
    }

  /* If not already specified, the mode alignment defaults to the largest
     power of two that divides the size of the object.  Complex types are
     not more aligned than their contents.  */
  if (m->class == MODE_COMPLEX_INT || m->class == MODE_COMPLEX_FLOAT)
    alignment = m->component->bytesize;
  else
    alignment = m->bytesize;

  m->alignment = alignment & (~alignment + 1);
}

static void
complete_all_modes (void)
{
  struct mode_data *m;
  enum mode_class c;
  
  for_all_modes (c, m)
    complete_mode (m);
}

/* For each mode in class CLASS, construct a corresponding complex mode.  */
#define COMPLEX_MODES(C) make_complex_modes(MODE_##C, __FILE__, __LINE__)
static void
make_complex_modes (enum mode_class class,
		    const char *file, unsigned int line)
{
  struct mode_data *m;
  struct mode_data *c;
  char buf[8];
  enum mode_class cclass = complex_class (class);

  if (cclass == MODE_RANDOM)
    return;
    
  for (m = known_modes[class]; m; m = m->next)
    {
      /* Skip BImode.  FIXME: BImode probably shouldn't be MODE_INT.  */
      if (m->bitsize == 1)
	continue;

      if (strlen (m->name) >= sizeof buf)
	{
	  error ("%s:%d:mode name \"%s\" is too long",
		 m->file, m->line, m->name);
	  continue;
	}

      /* Float complex modes are named SCmode, etc.
	 Int complex modes are named CSImode, etc.
         This inconsistency should be eliminated.  */
      if (class == MODE_FLOAT)
	{
	  char *p;
	  strncpy (buf, m->name, sizeof buf);
	  p = strchr (buf, 'F');
	  if (p == 0)
	    {
	      error ("%s:%d: float mode \"%s\" has no 'F'",
		     m->file, m->line, m->name);
	      continue;
	    }

	  *p = 'C';
	}
      else
	snprintf (buf, sizeof buf, "C%s", m->name);

      c = new_mode (cclass, xstrdup (buf), file, line);
      c->component = m;
    }
}

/* For all modes in class CLASS, construct vector modes of width
   WIDTH, having as many components as necessary.  */
#define VECTOR_MODES(C, W) make_vector_modes(MODE_##C, W, __FILE__, __LINE__)
static void
make_vector_modes (enum mode_class class, unsigned int width,
		   const char *file, unsigned int line)
{
  struct mode_data *m;
  struct mode_data *v;
  char buf[8];
  unsigned int ncomponents;
  enum mode_class vclass = vector_class (class);

  if (vclass == MODE_RANDOM)
    return;

  for (m = known_modes[class]; m; m = m->next)
    {
      /* Do not construct vector modes with only one element, or
	 vector modes where the element size doesn't divide the full
	 size evenly.  */
      ncomponents = width / m->bytesize;
      if (ncomponents < 2)
	continue;
      if (width % m->bytesize)
	continue;

      /* Skip QFmode and BImode.  FIXME: this special case should
	 not be necessary.  */
      if (class == MODE_FLOAT && m->bytesize == 1)
	continue;
      if (class == MODE_INT && m->bitsize == 1)
	continue;

      if ((size_t)snprintf (buf, sizeof buf, "V%u%s", ncomponents, m->name)
	  >= sizeof buf)
	{
	  error ("%s:%d: mode name \"%s\" is too long",
		 m->file, m->line, m->name);
	  continue;
	}

      v = new_mode (vclass, xstrdup (buf), file, line);
      v->component = m;
      v->ncomponents = ncomponents;
    }
}

/* Input.  */

#define _SPECIAL_MODE(C, N) make_special_mode(MODE_##C, #N, __FILE__, __LINE__)
#define RANDOM_MODE(N) _SPECIAL_MODE (RANDOM, N)
#define CC_MODE(N) _SPECIAL_MODE (CC, N)

static void
make_special_mode (enum mode_class class, const char *name,
		   const char *file, unsigned int line)
{
  new_mode (class, name, file, line);
}

#define INT_MODE(N, Y) FRACTIONAL_INT_MODE (N, -1, Y)
#define FRACTIONAL_INT_MODE(N, B, Y) \
  make_int_mode (#N, B, Y, __FILE__, __LINE__)

static void
make_int_mode (const char *name,
	       unsigned int bitsize, unsigned int bytesize,
	       const char *file, unsigned int line)
{
  struct mode_data *m = new_mode (MODE_INT, name, file, line);
  m->bytesize = bytesize;
  m->bitsize = bitsize;
}

#define FLOAT_MODE(N, Y, F)             FRACTIONAL_FLOAT_MODE (N, -1, Y, F)
#define FRACTIONAL_FLOAT_MODE(N, B, Y, F) \
  make_float_mode (#N, B, Y, #F, __FILE__, __LINE__)

static void
make_float_mode (const char *name,
		 unsigned int bitsize, unsigned int bytesize,
		 const char *format,
		 const char *file, unsigned int line)
{
  struct mode_data *m = new_mode (MODE_FLOAT, name, file, line);
  m->bytesize = bytesize;
  m->bitsize = bitsize;
  m->format = format;
}

#define RESET_FLOAT_FORMAT(N, F) \
  reset_float_format (#N, #F, __FILE__, __LINE__)
static void ATTRIBUTE_UNUSED
reset_float_format (const char *name, const char *format,
		    const char *file, const char *line)
{
  struct mode_data *m = find_mode (MODE_FLOAT, name);
  if (!m)
    {
      error ("%s:%d: no mode \"%s\" in class FLOAT", file, line, name);
      return;
    }
  m->format = format;
}

/* Partial integer modes are specified by relation to a full integer mode.
   For now, we do not attempt to narrow down their bit sizes.  */
#define PARTIAL_INT_MODE(M) \
  make_partial_integer_mode (#M, "P" #M, -1, __FILE__, __LINE__)
static void ATTRIBUTE_UNUSED
make_partial_integer_mode (const char *base, const char *name,
			   unsigned int bitsize,
			   const char *file, unsigned int line)
{
  struct mode_data *m;
  struct mode_data *component = find_mode (MODE_INT, base);
  if (!component)
    {
      error ("%s:%d: no mode \"%s\" in class INT", file, line, name);
      return;
    }
  
  m = new_mode (MODE_PARTIAL_INT, name, file, line);
  m->bitsize = bitsize;
  m->component = component;
}

/* A single vector mode can be specified by naming its component
   mode and the number of components.  */
#define VECTOR_MODE(C, M, N) \
  make_vector_mode (MODE_##C, #M, N, __FILE__, __LINE__);
static void ATTRIBUTE_UNUSED
make_vector_mode (enum mode_class bclass,
		  const char *base,
		  unsigned int ncomponents,
		  const char *file, unsigned int line)
{
  struct mode_data *v;
  enum mode_class vclass = vector_class (bclass);
  struct mode_data *component = find_mode (bclass, base);
  char namebuf[8];

  if (vclass == MODE_RANDOM)
    return;
  if (component == 0)
    {
      error ("%s:%d: no mode \"%s\" in class %s",
	     file, line, base, mode_class_names[bclass] + 5);
      return;
    }

  if ((size_t)snprintf (namebuf, sizeof namebuf, "V%u%s",
			ncomponents, base) >= sizeof namebuf)
    {
      error ("%s:%d: mode name \"%s\" is too long",
	     base, file, line);
      return;
    }

  v = new_mode (vclass, xstrdup (namebuf), file, line);
  v->ncomponents = ncomponents;
  v->component = component;
}
  

static void
create_modes (void)
{
#include "machmode.def"
}

/* Processing.  */

/* Sort a list of modes into the order needed for the WIDER field:
   major sort by bitsize, minor sort by component bitsize.

   For instance:
     QI < HI < SI < DI < TI
     V4QI < V2HI < V8QI < V4HI < V2SI.

   If the bitsize is not set, sort by the bytesize.  A mode with
   bitsize set gets sorted before a mode without bitsize set, if
   they have the same bytesize; this is the right thing because
   the bitsize must always be smaller than the bytesize * BITS_PER_UNIT.
   We don't have to do anything special to get this done -- an unset
   bitsize shows up as (unsigned int)-1, i.e. UINT_MAX.  */
static int
cmp_modes (const void *a, const void *b)
{
  struct mode_data *m = *(struct mode_data **)a;
  struct mode_data *n = *(struct mode_data **)b;

  if (m->bytesize > n->bytesize)
    return 1;
  else if (m->bytesize < n->bytesize)
    return -1;

  if (m->bitsize > n->bitsize)
    return 1;
  else if (m->bitsize < n->bitsize)
    return -1;

  if (!m->component && !n->component)
    return 0;

  if (m->component->bytesize > n->component->bytesize)
    return 1;
  else if (m->component->bytesize < n->component->bytesize)
    return -1;

  if (m->component->bitsize > n->component->bitsize)
    return 1;
  else if (m->component->bitsize < n->component->bitsize)
    return -1;

  return 0;
}

static void
calc_wider_mode (void)
{
  enum mode_class c;
  struct mode_data *m;
  struct mode_data **sortbuf;
  unsigned int max_n_modes = 0;
  unsigned int i, j;

  for (c = 0; c < MAX_MODE_CLASS; c++)
    max_n_modes = MAX (max_n_modes, n_modes[c]);

  /* Allocate max_n_modes + 1 entries to leave room for the extra null
     pointer assigned after the qsort call below.  */
  sortbuf = alloca ((max_n_modes + 1) * sizeof (struct mode_data *));

  for (c = 0; c < MAX_MODE_CLASS; c++)
    {
      /* "wider" is not meaningful for MODE_RANDOM and MODE_CC.
	 However, we want these in textual order, and we have
	 precisely the reverse.  */
      if (c == MODE_RANDOM || c == MODE_CC)
	{
	  struct mode_data *prev, *next;

	  for (prev = 0, m = known_modes[c]; m; m = next)
	    {
	      m->wider = void_mode;

	      /* this is nreverse */
	      next = m->next;
	      m->next = prev;
	      prev = m;
	    }
	  known_modes[c] = prev;
	}
      else
	{
	  if (!known_modes[c])
	    continue;

	  for (i = 0, m = known_modes[c]; m; i++, m = m->next)
	    sortbuf[i] = m;

	  qsort (sortbuf, i, sizeof (struct mode_data *), cmp_modes);

	  sortbuf[i] = 0;
	  for (j = 0; j < i; j++)
	    sortbuf[j]->next = sortbuf[j]->wider = sortbuf[j + 1];


	  known_modes[c] = sortbuf[0];
	}
    }
}

/* Output routines.  */

#define tagged_printf(FMT, ARG, TAG) do {		\
  int count_;						\
  printf ("  " FMT ",%n", ARG, &count_);		\
  printf ("%*s/* %s */\n", 27 - count_, "", TAG);	\
} while (0)

#define print_decl(TYPE, NAME, ASIZE) \
  printf ("\nconst %s %s[%s] =\n{\n", TYPE, NAME, ASIZE);

#define print_closer() puts ("};")

static void
emit_insn_modes_h (void)
{
  enum mode_class c;
  struct mode_data *m, *first, *last;

  printf ("/* Generated automatically from machmode.def%s%s\n",
	   HAVE_EXTRA_MODES ? " and " : "",
	   EXTRA_MODES_FILE);

  puts ("\
   by genmodes.  */\n\
\n\
#ifndef GCC_INSN_MODES_H\n\
#define GCC_INSN_MODES_H\n\
\n\
enum machine_mode\n{");

  for (c = 0; c < MAX_MODE_CLASS; c++)
    for (m = known_modes[c]; m; m = m->next)
      {
	int count_;
	printf ("  %smode,%n", m->name, &count_);
	printf ("%*s/* %s:%d */\n", 27 - count_, "",
		 trim_filename (m->file), m->line);
      }

  puts ("  MAX_MACHINE_MODE,\n");

  for (c = 0; c < MAX_MODE_CLASS; c++)
    {
      first = known_modes[c];
      last = 0;
      for (m = first; m; last = m, m = m->next)
	;

      /* Don't use BImode for MIN_MODE_INT, since otherwise the middle
	 end will try to use it for bitfields in structures and the
	 like, which we do not want.  Only the target md file should
	 generate BImode widgets.  */
      if (first && first->bitsize == 1)
	first = first->next;

      if (first && last)
	printf ("  MIN_%s = %smode,\n  MAX_%s = %smode,\n\n",
		 mode_class_names[c], first->name,
		 mode_class_names[c], last->name);
      else
	printf ("  MIN_%s = %smode,\n  MAX_%s = %smode,\n\n",
		 mode_class_names[c], void_mode->name,
		 mode_class_names[c], void_mode->name);
    }

  puts ("\
  NUM_MACHINE_MODES = MAX_MACHINE_MODE\n\
};\n\
\n\
#endif /* insn-modes.h */");
}

static void
emit_insn_modes_c_header (void)
{
  printf ("/* Generated automatically from machmode.def%s%s\n",
	   HAVE_EXTRA_MODES ? " and " : "",
	   EXTRA_MODES_FILE);

  puts ("\
   by genmodes.  */\n\
\n\
#include \"config.h\"\n\
#include \"system.h\"\n\
#include \"coretypes.h\"\n\
#include \"tm.h\"\n\
#include \"machmode.h\"\n\
#include \"real.h\"");
}

static void
emit_min_insn_modes_c_header (void)
{
  printf ("/* Generated automatically from machmode.def%s%s\n",
	   HAVE_EXTRA_MODES ? " and " : "",
	   EXTRA_MODES_FILE);

  puts ("\
   by genmodes.  */\n\
\n\
#include \"bconfig.h\"\n\
#include \"system.h\"\n\
#include \"machmode.h\"");
}

static void
emit_mode_name (void)
{
  enum mode_class c;
  struct mode_data *m;

  print_decl ("char *const", "mode_name", "NUM_MACHINE_MODES");

  for_all_modes (c, m)
    printf ("  \"%s\",\n", m->name);

  print_closer ();
}

static void
emit_mode_class (void)
{
  enum mode_class c;
  struct mode_data *m;

  print_decl ("unsigned char", "mode_class", "NUM_MACHINE_MODES");

  for_all_modes (c, m)
    tagged_printf ("%s", mode_class_names[m->class], m->name);

  print_closer ();
}

static void
emit_mode_bitsize (void)
{
  enum mode_class c;
  struct mode_data *m;

  print_decl ("unsigned short", "mode_bitsize", "NUM_MACHINE_MODES");

  for_all_modes (c, m)
    if (m->bitsize != (unsigned int)-1)
      tagged_printf ("%u", m->bitsize, m->name);
    else
      tagged_printf ("%u*BITS_PER_UNIT", m->bytesize, m->name);

  print_closer ();
}

static void
emit_mode_size (void)
{
  enum mode_class c;
  struct mode_data *m;

  print_decl ("unsigned char", "mode_size", "NUM_MACHINE_MODES");

  for_all_modes (c, m)
    tagged_printf ("%u", m->bytesize, m->name);

  print_closer ();
}

static void
emit_mode_unit_size (void)
{
  enum mode_class c;
  struct mode_data *m;

  print_decl ("unsigned char", "mode_unit_size", "NUM_MACHINE_MODES");

  for_all_modes (c, m)
    tagged_printf ("%u",
		   m->component
		   ? m->component->bytesize : m->bytesize,
		   m->name);

  print_closer ();
}

static void
emit_mode_wider (void)
{
  enum mode_class c;
  struct mode_data *m;

  print_decl ("unsigned char", "mode_wider", "NUM_MACHINE_MODES");

  for_all_modes (c, m)
    tagged_printf ("%smode",
		   m->wider ? m->wider->name : void_mode->name,
		   m->name);

  print_closer ();
}

static void
emit_mode_mask (void)
{
  enum mode_class c;
  struct mode_data *m;

  print_decl ("unsigned HOST_WIDE_INT", "mode_mask_array",
	      "NUM_MACHINE_MODES");
  puts ("\
#define MODE_MASK(m)                          \\\n\
  ((m) >= HOST_BITS_PER_WIDE_INT)             \\\n\
   ? ~(unsigned HOST_WIDE_INT) 0              \\\n\
   : ((unsigned HOST_WIDE_INT) 1 << (m)) - 1\n");

  for_all_modes (c, m)
    if (m->bitsize != (unsigned int)-1)
      tagged_printf ("MODE_MASK (%u)", m->bitsize, m->name);
    else
      tagged_printf ("MODE_MASK (%u*BITS_PER_UNIT)", m->bytesize, m->name);

  puts ("#undef MODE_MASK");
  print_closer ();
}

static void
emit_mode_inner (void)
{
  enum mode_class c;
  struct mode_data *m;

  print_decl ("unsigned char", "mode_inner", "NUM_MACHINE_MODES");

  for_all_modes (c, m)
    tagged_printf ("%smode",
		   m->component ? m->component->name : void_mode->name,
		   m->name);

  print_closer ();
}

static void
emit_mode_base_align (void)
{
  enum mode_class c;
  struct mode_data *m;

  print_decl ("unsigned char", "mode_base_align", "NUM_MACHINE_MODES");

  for_all_modes (c, m)
    tagged_printf ("%u", m->alignment, m->name);

  print_closer ();
}

static void
emit_class_narrowest_mode (void)
{
  enum mode_class c;

  print_decl ("unsigned char", "class_narrowest_mode", "MAX_MODE_CLASS");

  for (c = 0; c < MAX_MODE_CLASS; c++)
    /* Bleah, all this to get the comment right for MIN_MODE_INT.  */
    tagged_printf ("MIN_%s", mode_class_names[c],
		   known_modes[c]
		   ? (known_modes[c]->bitsize != 1
		      ? known_modes[c]->name
		      : (known_modes[c]->next
			 ? known_modes[c]->next->name
			 : void_mode->name))
		   : void_mode->name);
  
  print_closer ();
}

static void
emit_real_format_for_mode (void)
{
  struct mode_data *m;

  /* This will produce a table which is not constant, but points to
     entities that are constant, which is what we want.  */
  print_decl ("struct real_format *\n ", "real_format_for_mode",
	      "MAX_MODE_FLOAT - MIN_MODE_FLOAT + 1");

  for (m = known_modes[MODE_FLOAT]; m; m = m->next)
    if (!strcmp (m->format, "0"))
      tagged_printf ("%s", m->format, m->name);
    else
      tagged_printf ("&%s", m->format, m->name);

  print_closer ();
}

static void
emit_insn_modes_c (void)
{
  emit_insn_modes_c_header ();
  emit_mode_name ();
  emit_mode_class ();
  emit_mode_bitsize ();
  emit_mode_size ();
  emit_mode_unit_size ();
  emit_mode_wider ();
  emit_mode_mask ();
  emit_mode_inner ();
  emit_mode_base_align ();
  emit_class_narrowest_mode ();
  emit_real_format_for_mode ();
}

static void
emit_min_insn_modes_c (void)
{
  emit_min_insn_modes_c_header ();
  emit_mode_name ();
  emit_mode_class ();
  emit_mode_wider ();
  emit_class_narrowest_mode ();
}

/* Master control.  */
int
main(int argc, char **argv)
{
  bool gen_header = false, gen_min = false;
  progname = argv[0];

  if (argc == 1)
    ;
  else if (argc == 2 && !strcmp (argv[1], "-h"))
    gen_header = true;
  else if (argc == 2 && !strcmp (argv[1], "-m"))
    gen_min = true;
  else
    {
      error ("usage: %s [-h|-m] > file", progname);
      return FATAL_EXIT_CODE;
    }

  create_modes ();
  complete_all_modes ();

  if (have_error)
    return FATAL_EXIT_CODE;
  
  calc_wider_mode ();

  if (gen_header)
    emit_insn_modes_h ();
  else if (gen_min)
    emit_min_insn_modes_c ();
  else
    emit_insn_modes_c ();

  if (fflush (stdout) || fclose (stdout))
    return FATAL_EXIT_CODE;
  return SUCCESS_EXIT_CODE;
}
