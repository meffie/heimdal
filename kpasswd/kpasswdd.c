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

#include "kpasswd_locl.h"
#include <hdb.h>
RCSID("$Id$");

static void
syslog_and_die (const char *m, ...)
{
    va_list args;

    va_start(args, m);
    vsyslog (LOG_ERR, m, args);
    va_end(args);
    exit (1);
}

static char *database = HDB_DEFAULT_DB;

static void
send_reply (int s,
	    struct sockaddr_in *addr,
	    krb5_data *ap_rep,
	    krb5_data *rest)
{
    struct msghdr msghdr;
    struct iovec iov[3];
    u_int16_t len, ap_rep_len;
    u_char header[6];
    u_char *p;

    if (ap_rep)
	ap_rep_len = ap_rep->length;
    else
	ap_rep_len = 0;

    len = 6 + ap_rep_len + rest->length;
    p = header;
    *p++ = (len >> 8) & 0xFF;
    *p++ = (len >> 0) & 0xFF;
    *p++ = 0;
    *p++ = 1;
    *p++ = (ap_rep_len >> 8) & 0xFF;
    *p++ = (ap_rep_len >> 0) & 0xFF;

    memset (&msghdr, 0, sizeof(msghdr));
    msghdr.msg_name       = (void *)addr;
    msghdr.msg_namelen    = sizeof(*addr);
    msghdr.msg_iov        = iov;
    msghdr.msg_iovlen     = sizeof(iov)/sizeof(*iov);
#if 0
    msghdr.msg_control    = NULL;
    msghdr.msg_controllen = 0;
#endif

    iov[0].iov_base       = header;
    iov[0].iov_len        = 6;
    if (ap_rep_len) {
	iov[1].iov_base   = ap_rep->data;
	iov[1].iov_len    = ap_rep->length;
    } else {
	iov[1].iov_base   = NULL;
	iov[1].iov_len    = 0;
    }
    iov[2].iov_base       = rest->data;
    iov[2].iov_len        = rest->length;

    if (sendmsg (s, &msghdr, 0) < 0)
	syslog (LOG_ERR, "sendmsg: %m");
}

static int
make_result (krb5_data *data,
	     u_int16_t result_code,
	     const char *expl)
{
    krb5_data_zero (data);

    data->length = asprintf ((char **)&data->data,
			     "%c%c%s",
			     (result_code >> 8) & 0xFF,
			     result_code & 0xFF,
			     expl);

    if (data->data == NULL) {
	syslog (LOG_ERR, "Out of memory generating error reply");
	return 1;
    }
    return 0;
}

static void
reply_error (krb5_context context,
	     krb5_principal server,
	     int s,
	     struct sockaddr_in *addr,
	     krb5_error_code error_code,
	     u_int16_t result_code,
	     const char *expl)
{
    krb5_error_code ret;
    krb5_data error_data;
    krb5_data e_data;

    if (make_result(&e_data, result_code, expl))
	return;

    ret = krb5_mk_error (context,
			 error_code,
			 NULL,
			 &e_data,
			 NULL,
			 server,
			 0,
			 &error_data);
    krb5_data_free (&e_data);
    if (ret) {
	syslog (LOG_ERR,
		"Could not even generate error reply: %s",
		krb5_get_err_text (context, ret));
	return;
    }
    send_reply (s, addr, NULL, &error_data);
    krb5_data_free (&error_data);
}

static void
reply_priv (krb5_context context,
	    krb5_auth_context auth_context,
	    int s,
	    struct sockaddr_in *addr,
	    u_int16_t result_code,
	    const char *expl)
{
    krb5_error_code ret;
    krb5_data krb_priv_data;
    krb5_data ap_rep_data;
    krb5_data e_data;

    ret = krb5_mk_rep (context,
		       &auth_context,
		       &ap_rep_data);
    if (ret) {
	syslog (LOG_ERR,
		"Could not even generate error reply: %s",
		krb5_get_err_text (context, ret));
	return;
    }

    if (make_result(&e_data, result_code, expl))
	return;

    ret = krb5_mk_priv (context,
			auth_context,
			&e_data,
			&krb_priv_data,
			NULL);
    krb5_data_free (&e_data);
    if (ret) {
	syslog (LOG_ERR,
		"Could not even generate error reply: %s",
		krb5_get_err_text (context, ret));
	return;
    }
    send_reply (s, addr, &ap_rep_data, &krb_priv_data);
    krb5_data_free (&ap_rep_data);
    krb5_data_free (&krb_priv_data);
}

static void
change (krb5_context context,
	krb5_auth_context auth_context,
	krb5_principal principal,
	int s,
	struct sockaddr_in *addr,
	krb5_data *pwd_data)
{
    krb5_error_code ret;
    char *c;
    HDB *db;
    hdb_entry ent;
    krb5_data salt;
    krb5_keyblock new_keyblock;

    krb5_unparse_name (context, principal, &c);

    syslog (LOG_INFO, "Changing password for %s", c);
    free (c);

    if (pwd_data->length < 6) {	/* XXX */
	syslog (LOG_ERR, "Password too short");
	reply_priv (context, auth_context, s, addr, 4, "password too short");
	return;
    }

    ret = hdb_open (context, &db, database, O_RDWR, 0600);
    if (ret) {
	syslog (LOG_ERR, "hdb_open: %s", krb5_get_err_text(context, ret));
	reply_priv (context, auth_context, s, addr, 2, "hdb_open failed");
	return;
    }

    ent.principal = principal;

    ret = db->fetch (context, db, &ent);
    
    switch (ret) {
    case KRB5_HDB_NOENTRY:
	syslog (LOG_ERR, "not found in database");
	reply_priv (context, auth_context, s, addr, 2,
		    "entry not found in database");
	goto out;
    case 0:
	break;
    default :
	syslog (LOG_ERR, "dbfetch: %s", krb5_get_err_text(context, ret));
	reply_priv (context, auth_context, s, addr, 2,
		    "db_fetch failed");
	goto out;
    }

    krb5_data_zero (&salt);
    krb5_get_salt (principal, &salt);
    memset (&new_keyblock, 0, sizeof(new_keyblock));
    krb5_string_to_key_data (pwd_data, &salt, &new_keyblock);
    krb5_data_free (&salt);
    if (new_keyblock.keytype == ent.keyblock.keytype
	&& new_keyblock.keyvalue.length == ent.keyblock.keyvalue.length
	&& memcmp (new_keyblock.keyvalue.data,
		   ent.keyblock.keyvalue.data,
		   new_keyblock.keyvalue.length) == 0) {
	ret = 0;
    } else {
	memset (&ent.keyblock, 0, sizeof(ent.keyblock));
	ent.keyblock.keytype = new_keyblock.keytype;
	krb5_data_copy (&ent.keyblock.keyvalue,
			new_keyblock.keyvalue.data,
			new_keyblock.keyvalue.length);
	ent.kvno++;
	ent.last_change = time(NULL);
	krb5_copy_principal (context, principal, &ent.changed_by);
	ret = db->store (context, db, &ent);
    }
    memset (&new_keyblock, 0, sizeof(new_keyblock));
    krb5_free_keyblock (context, &new_keyblock);

    if (ret == -1) {
	syslog (LOG_ERR, "dbstore: %m");
	reply_priv (context, auth_context, s, addr, 2,
		    "db_store failed");
    } else {
	reply_priv (context, auth_context, s, addr, 0, "password changed");
    }
out:
    hdb_free_entry (context, &ent);
    db->close (context, db);
}

static int
verify (krb5_context context,
	krb5_auth_context *auth_context,
	krb5_principal server,
	krb5_ticket **ticket,
	krb5_data *out_data,
	int s,
	struct sockaddr_in *addr,
	u_char *msg,
	size_t len)
{
    krb5_error_code ret;
    u_int16_t pkt_len, pkt_ver, ap_req_len;
    krb5_data ap_req_data;
    krb5_data krb_priv_data;

    pkt_len = (msg[0] << 8) | (msg[1]);
    pkt_ver = (msg[2] << 8) | (msg[3]);
    ap_req_len = (msg[4] << 8) | (msg[5]);
    if (pkt_len != len) {
	syslog (LOG_ERR, "Strange len: %d != %d", pkt_len, len);
	reply_error (context, server, s, addr, 0, 1, "bad length");
	return 1;
    }
    if (pkt_ver != 0x0001) {
	syslog (LOG_ERR, "Bad version (%d)", pkt_ver);
	reply_error (context, server, s, addr, 0, 1, "bad version");
	return 1;
    }

    ap_req_data.data   = msg + 6;
    ap_req_data.length = ap_req_len;

    ret = krb5_rd_req (context,
		       auth_context,
		       &ap_req_data,
		       server,
		       NULL,
		       NULL,
		       ticket);
    if (ret) {
	syslog (LOG_ERR, "krb5_rd_req: %s",
		krb5_get_err_text(context, ret));
	reply_error (context, server, s, addr, ret, 3, "rd_req failed");
	return 1;
    }

    if (!(*ticket)->ticket.flags.initial) {
	syslog (LOG_ERR, "initial flag not set");
	reply_error (context, server, s, addr, ret, 1,
		     "initial flag not set");
	krb5_free_ticket (context, *ticket);
	goto out;
    }
    krb_priv_data.data   = msg + 6 + ap_req_len;
    krb_priv_data.length = len - 6 - ap_req_len;

    ret = krb5_rd_priv (context,
			*auth_context,
			&krb_priv_data,
			out_data,
			NULL);
    
    if (ret) {
	syslog (LOG_ERR, "krb5_rd_priv: %s",
		krb5_get_err_text(context, ret));
	reply_error (context, server, s, addr, ret, 3, "rd_priv failed");
	goto out;
    }
    return 0;
out:
    krb5_free_ticket (context, *ticket);
    return 1;
}

static void
process (krb5_context context,
	 krb5_principal server,
	 int s,
	 struct sockaddr_in *addr,
	 u_char *msg,
	 int len)
{
    krb5_error_code ret;
    krb5_auth_context auth_context = NULL;
    krb5_data out_data;
    krb5_ticket *ticket;

    krb5_data_zero (&out_data);

    if (verify (context, &auth_context, server, &ticket, &out_data,
		s, addr, msg, len) == 0)
	change (context,
		auth_context,
		ticket->client,
		s,
		addr,
		&out_data);

    krb5_data_free (&out_data);
    krb5_auth_con_free (context, auth_context);
}

static void
doit (int port)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_principal server;
    struct sockaddr_in addr;
    int s;
    char *realm;

    ret = krb5_init_context (&context);
    if (ret)
	syslog_and_die ("krb5_init_context: %s",
			krb5_get_err_text(context, ret));

    ret = krb5_get_default_realm (context, &realm);
    if (ret)
	syslog_and_die ("krb5_get_default_realm: %s",
			krb5_get_err_text(context, ret));

    ret = krb5_build_principal_ext (context,
				    &server,
				    strlen(realm),
				    realm,
				    strlen("kadmin"),
				    "kadmin",
				    strlen("changepw"),
				    "changepw",
				    NULL);
    if (ret)
	syslog_and_die ("krb5_build_principal_ext: %s",
			krb5_get_err_text(context, ret));

    free (realm);

    s = socket (AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
	syslog_and_die ("socket: %m");
    memset (&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = port;
    if (bind (s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	syslog_and_die ("bind: %m");
    for (;;) {
	struct sockaddr_in other_addr;
	u_char buf[BUFSIZ];
	int ret;
	int addrlen = sizeof(other_addr);

	ret = recvfrom (s, buf, sizeof(buf), 0,
			(struct sockaddr *)&other_addr,
			&addrlen);
	if (ret < 0)
	    if(errno == EINTR)
		continue;
	    else
		syslog_and_die ("recvfrom: %m");
	process (context, server, s, &other_addr, buf, ret);
    }
}

int
main (int argc, char **argv)
{
    set_progname (argv[0]);
    openlog ("kpasswdd", LOG_ODELAY | LOG_PID, LOG_AUTH);

    doit (krb5_getportbyname ("kpasswd", "udp", htons(KPASSWD_PORT)));
    return 0;
}
