/* ====================================================================
 * Copyright (c) 1994-2000 Carnegie Mellon University.  All rights 
 * reserved.
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
 * 3. The names "Sphinx" and "Carnegie Mellon" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. To obtain permission, contact 
 *    sphinx@cs.cmu.edu.
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Carnegie
 *    Mellon University (http://www.speech.cs.cmu.edu/)."
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/*********************************************************************
 *
 * File: main.c
 * 
 * Description: 
 * 	Tie states according to senone decision trees
 *
 * Author: 
 * 	Eric Thayer (eht@cs.cmu.edu)
 *********************************************************************/

#include "parse_cmd_ln.h"

#include <s3/model_def_io.h>
#include <s3/dtree.h>
#include <s3/pset_io.h>
#include <s3/ckd_alloc.h>
#include <s3/cmd_ln.h>
#include <s3/err.h>
#include <s3/s3.h>

#include <sys_compat/file.h>

#include <assert.h>


int
init(model_def_t **out_imdef,
     pset_t **out_pset,
     uint32 *out_n_pset,
     dtree_t ****out_tree,
     uint32 *out_n_seno)
{
    model_def_t *imdef;
    uint32 p, s;
    uint32 n_ci, n_state;
    char fn[MAXPATHLEN+1];
    const char *a_fn;
    FILE *fp;
    dtree_t ***tree, *tr;
    pset_t *pset;
    uint32 n_pset;
    uint32 n_seno;
    const char *treedir;
    uint32 ts_id;

    a_fn = (const char *)cmd_ln_access("-imoddeffn");
    if (a_fn == NULL)
	E_FATAL("Specify -imoddeffn\n");
    if (model_def_read(&imdef, a_fn) != S3_SUCCESS) {
	return S3_ERROR;
    }
    *out_imdef = imdef;

    a_fn = (const char *)cmd_ln_access("-psetfn");
    E_INFO("Reading: %s\n", a_fn);
    *out_pset = pset = read_pset_file(a_fn, imdef->acmod_set, &n_pset);
    *out_n_pset = n_pset;

    n_ci = acmod_set_n_ci(imdef->acmod_set);

    treedir = (const char *)cmd_ln_access("-treedir");
    tree = (dtree_t ***)ckd_calloc(n_ci, sizeof(dtree_t **));
    *out_tree = tree;

    ts_id = imdef->n_tied_ci_state;
    for (p = 0, n_seno = 0; p < n_ci; p++) {
	if (!acmod_set_has_attrib(imdef->acmod_set, p, "filler")) {
	    n_state = imdef->defn[p].n_state;

	    tree[p] = (dtree_t **)ckd_calloc(n_state, sizeof(dtree_t *));

	    for (s = 0; s < n_state-1; s++) {
		E_INFO("%s-%u: offset %u\n",
		       acmod_set_id2name(imdef->acmod_set, p), s, ts_id);

		sprintf(fn, "%s/%s-%u.dtree",
			treedir, acmod_set_id2name(imdef->acmod_set, p), s);
		fp = fopen(fn, "r");
		if (fp == NULL) {
		    E_FATAL_SYSTEM("Unable to open %s for reading", fn);
		}
		tree[p][s] = tr = read_final_tree(fp, pset, n_pset);

		label_leaves(&tr->node[0], &ts_id);

		fclose(fp);

		n_seno += cnt_leaf(&tr->node[0]);
	    }
	}
    }

    assert(n_seno == (ts_id - imdef->n_tied_ci_state));

    E_INFO("n_seno= %u\n", ts_id);

    *out_n_seno = n_seno;

    return S3_SUCCESS;
}

int
main(int argc, char *argv[])
{
    model_def_t *imdef;
    model_def_t *omdef;
    pset_t *pset;
    uint32 n_pset;
    dtree_t ***tree;
    uint32 n_seno;
    uint32 n_ci;
    uint32 n_acmod;
    uint32 p;
    uint32 s;
    model_def_entry_t *idefn, *odefn;
    acmod_id_t b, l, r;
    word_posn_t wp;
    
    parse_cmd_ln(argc, argv);

    if (init(&imdef, &pset, &n_pset, &tree, &n_seno) != S3_SUCCESS)
	return 1;

    omdef = (model_def_t *)ckd_calloc(1, sizeof(model_def_t));

    omdef->acmod_set = imdef->acmod_set; /* same set of acoustic models */

    omdef->n_total_state = imdef->n_total_state;

    omdef->n_tied_ci_state = imdef->n_tied_ci_state;
    omdef->n_tied_state = imdef->n_tied_ci_state + n_seno;

    omdef->n_tied_tmat = imdef->n_tied_tmat;

    omdef->defn = (model_def_entry_t *)ckd_calloc(imdef->n_defn,
						  sizeof(model_def_entry_t));

    /*
     * Define the context-independent models
     */
    n_ci = acmod_set_n_ci(imdef->acmod_set);
    for (p = 0; p < n_ci; p++) {
	idefn = &imdef->defn[p];
	odefn = &omdef->defn[p];
	
	odefn->p    = idefn->p;
	odefn->tmat = idefn->tmat;

	odefn->state = ckd_calloc(idefn->n_state, sizeof(uint32));
	odefn->n_state = idefn->n_state;

	for (s = 0; s < idefn->n_state; s++) {
	    if (idefn->state[s] == NO_ID)
		odefn->state[s] = NO_ID;
	    else {
		odefn->state[s] = idefn->state[s];
	    }
	}
    }

    /*
     * Define the rest of the models
     */
    n_acmod = acmod_set_n_acmod(omdef->acmod_set);
    for (; p < n_acmod; p++) {
	b = acmod_set_base_phone(omdef->acmod_set, p);

	assert(p != b);

	idefn = &imdef->defn[p];
	odefn = &omdef->defn[p];

	odefn->p    = idefn->p;
	odefn->tmat = idefn->tmat;

	odefn->state = ckd_calloc(idefn->n_state, sizeof(uint32));
	odefn->n_state = idefn->n_state;
	for (s = 0; s < idefn->n_state; s++) {
	    if (idefn->state[s] == NO_ID)
		/* Non-emitting state */
		odefn->state[s] = NO_ID;
	    else {
		/* emitting state: find the tied state */
		acmod_set_id2tri(omdef->acmod_set,
				 &b, &l, &r, &wp,
				 p);
		fprintf(stderr, "%s %u ",
			acmod_set_id2name(omdef->acmod_set, p), s);

		odefn->state[s] = tied_state(&tree[b][s]->node[0],
					     b, l, r, wp,
					     pset);

		fprintf(stderr, "\t-> %u\n", odefn->state[s]);

		fprintf(stderr, "\n");
	    }
	}
    }

    if (model_def_write(omdef, (const char *)cmd_ln_access("-omoddeffn")) != S3_SUCCESS) {
	return 1;
    }

    return 0;
}

/*
 * Log record.  Maintained by RCS.
 *
 * $Log$
 * Revision 1.4  2004/06/17  19:17:24  arthchan2003
 * Code Update for silence deletion and standardize the name for command -line arguments
 * 
 * Revision 1.3  2001/04/05 20:02:31  awb
 * *** empty log message ***
 *
 * Revision 1.2  2000/09/29 22:35:14  awb
 * *** empty log message ***
 *
 * Revision 1.1  2000/09/24 21:38:32  awb
 * *** empty log message ***
 *
 * Revision 1.1  97/07/16  11:36:22  eht
 * Initial revision
 * 
 *
 */
