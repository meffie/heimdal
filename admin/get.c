/*
 * Copyright (c) 1997 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. All advertising materials mentioning features or use of this software 
 *    must display the following acknowledgement: 
 *      This product includes software developed by Kungliga Tekniska 
 *      H�gskolan and its contributors. 
 *
 * 4. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "admin_locl.h"

RCSID("$Id$");

int
get_entry(int argc, char **argv)
{
    HDB *db;
    int err;
    hdb_entry ent;
    
    if(argc != 2){
	fprintf(stderr, "Usage: get_entry principal\n");
	return 0;
    }
	
    krb5_parse_name(context, argv[1], &ent.principal);
    
    if((err = hdb_open(context, &db, database, O_RDONLY, 0600))){
	fprintf(stderr, "hdb_open: %s\n", krb5_get_err_text(context, err));
	return 0;
    }
    
    err = db->fetch(context, db, &ent);
    
    switch(err){
    case KRB5_HDB_NOENTRY:
	fprintf(stderr, "Entry not found in database\n");
	break;
    case 0: {
	char *name;
	krb5_unparse_name(context, ent.principal, &name);
	printf("Principal: %s\n", name);
	free(name);
	printf("Max ticket life: %d\n", ent.max_life);
	printf("Max renewable ticket life: %d\n", ent.max_renew);
	printf("Key type: ");
	if(ent.keyblock.keytype == KEYTYPE_DES)
	    printf("DES");
	else
	    printf("%d", (int)ent.keyblock.keytype);
	printf("\tKvno: %d\n", ent.kvno);
	break;
    }
    default:
	fprintf(stderr, "dbget: %s\n", krb5_get_err_text(context, err));;
	break;
    }
    memset(&ent, 0, sizeof(ent));
    db->close(context, db);
    return 0;
}
