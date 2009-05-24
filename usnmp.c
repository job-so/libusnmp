/*
 * ulibsnmp.c
 *
 *  Created on: 14 mai 2009
 *      Author: yannick
 */
#include "include/usnmp.h"
inline void usnmp_init() {
	srand(getpid());
}

inline void usnmp_init_device(usnmp_device_t * device) {
	memset(device, 0, sizeof(usnmp_device_t));
}

/* return a pdu structure, you can free after use (with usnmp_clean_pdu and free function)
 * after send for exemple */
inline usnmp_pdu_t * usnmp_create_pdu(int op, usnmp_version_t ver) {
	usnmp_pdu_t *pdu = (usnmp_pdu_t *) calloc(1, sizeof(usnmp_pdu_t));
	if (pdu == NULL) {
		//TODO error
	}
	pdu->type = op;
	pdu->version = ver;
	pdu->error_status = 0;
	pdu->error_index = 0;
	pdu->nbindings = 0;
	return pdu;
}
/* free all sub element of usnmp_pdu_t use with usnmp_*_pdu to change pdu.
 * Warning : this function should be used only if you don't change something yourself */
inline void usnmp_clean_pdu(usnmp_pdu_t *pdu) {
	u_int i;
	for (i = 0; i < pdu->nbindings; i++)
		usnmp_value_free(&pdu->bindings[i]);
}

inline void usnmp_clean_var(usnmp_var_t *var) {
	usnmp_value_free(var);
}
/**
 * create a variable, param value is a ptr to the value.
 * value special type :
 * 		ip : value is u_char[4] (ipv6 not supported)
 * 		oid : value is usnmp_oid_t
 *
 */
inline usnmp_var_t * usnmp_create_var(usnmp_oid_t oid, usnmp_type_t type,
		void * value) {
	usnmp_var_t * var = (usnmp_var_t *) calloc(1, sizeof(usnmp_var_t));
	var->var = oid;
	int i = 0;
	if (NULL!=var) {
		var->syntax = type;
		switch (type) {
		case USNMP_SYNTAX_INTEGER:
			var->v.integer = *(int32_t*) value;
			break;
		case USNMP_SYNTAX_OCTETSTRING:
			var->v.octetstring.len = ((struct octetstring_st *) value)->len;
			/*fprintf(stdout, "OCTET STRING %ui:", var->v.octetstring.len);*/
			for (i = 0; i < var->v.octetstring.len; i++)
				/*fprintf(stdout, " %02x", var->v.octetstring.octets[i]); */
				var->v.octetstring.octets[i]
						= ((struct octetstring_st *) value)->octets[i];
			break;
		case USNMP_SYNTAX_OID:
			// TODO check
			/*fprintf(stdout, "OID %s", asn_oid2str_r(&var->v.oid, buf));*/
			var->v.oid.len = ((usnmp_oid_t *) value)->len;
			memcpy(var->v.oid.subs, ((usnmp_oid_t *) value)->subs,
					sizeof(asn_subid_t) * ASN_MAXOIDLEN);
			break;
		case USNMP_SYNTAX_IPADDRESS:
			// TODO check
			memcpy(var->v.ipaddress, value, sizeof(u_char) * 4);
			break;
		case USNMP_SYNTAX_COUNTER:
		case USNMP_SYNTAX_GAUGE:
		case USNMP_SYNTAX_TIMETICKS:
			var->v.uint32 = *(u_int32_t *) value;
			break;
		case USNMP_SYNTAX_COUNTER64:
			var->v.counter64 = *(u_int64_t*) value;
			break;
		case USNMP_SYNTAX_NULL:
		case USNMP_SYNTAX_NOSUCHOBJECT:
		case USNMP_SYNTAX_NOSUCHINSTANCE:
		case USNMP_SYNTAX_ENDOFMIBVIEW:
			break;
		default:
			fprintf(stderr, "UNKNOWN SYNTAX %u", var->syntax);
			free(var);
			var = NULL;
			break;
		}
	}
	return var;
}

inline usnmp_var_t * usnmp_create_null_var(usnmp_oid_t oid) {
	return usnmp_create_var(oid, USNMP_SYNTAX_NULL, NULL);
}

/* add variable to pdu, you can free usnmp_var_t after add to pdu,
 * return -1 if max binding is already reach */
int usnmp_add_variable_to_pdu(usnmp_pdu_t * pdu, usnmp_var_t * var) {
	int ret = pdu->nbindings;
	if (pdu->nbindings >= USNMP_MAX_BINDINGS) {
		/* TODO error */
		return (-1);
	}
	pdu->bindings[pdu->nbindings].var = var->var;
	pdu->bindings[pdu->nbindings].syntax = var->syntax;
	pdu->bindings[pdu->nbindings].v = var->v;
	pdu->nbindings++;
	return ret;
}
/* return a usnmp_var_t[] free this after use */
usnmp_list_var_t * usnmp_get_var_list_from_pdu(usnmp_pdu_t * pdu) {
	usnmp_list_var_t *lvar = (usnmp_list_var_t *) calloc(1,
			sizeof(usnmp_list_var_t));
	usnmp_list_var_t *cur = lvar;
	int i = 0;
	for (i = 0; i < pdu->nbindings; i++) {
		usnmp_value_copy(&lvar->var, &pdu->bindings[i]);
		cur->next = (usnmp_list_var_t *) calloc(1, sizeof(usnmp_list_var_t));
		if (NULL==cur->next) {
			while (NULL!=lvar) {
				cur = lvar;
				lvar = lvar->next;
				usnmp_value_free(&cur->var);
				free(cur);
			}
			break;
		}
		cur = cur->next;
	}
	return lvar;
}

/* this function is thread safe. they drop all other packet
 */
int usnmp_sync_send_pdu(usnmp_pdu_t pdu_send, usnmp_pdu_t ** pdu_recv,
		usnmp_socket_t *psocket, struct timeval *timeout, usnmp_device_t dev) {
	usnmp_socket_t *rsocket = NULL;
	if (NULL==psocket) {
		rsocket = usnmp_create_and_open_socket(USNMP_RANDOM_PORT,NULL);
	} else {
		rsocket=psocket;
	}
	/* send packet */
	if(0==pthread_mutex_trylock(&rsocket->lockme)) {
		/* TODO error */
		fprintf(stderr,"socket is busy");
		return -1;
	}
	u_int32_t reqid=usnmp_send_pdu(&pdu_send,rsocket,dev);
	if(reqid==0) {
		/* TODO error sending */
		return -1;
	}
	/* waiting for a response drop all other packet */
	do {
		if(0>(usnmp_recv_pdu(pdu_recv,timeout,rsocket))) {
			/* TODO error while recving */
			/* TODO define if timeout or other*/
			return -1;
		}
	}while(reqid!=(*pdu_recv)->request_id);
	pthread_mutex_unlock(&rsocket->lockme);
	if(NULL==psocket) {
		free(rsocket);
	}
	return 0;
}

		/* forge le packet */
inline int usnmp_build_packet(usnmp_pdu_t * pdu, u_char *sndbuf, size_t *sndlen) {
	struct asn_buf resp_b;

	resp_b.asn_ptr = sndbuf;
	resp_b.asn_len = USNMP_MAX_MSG_SIZE;

	if (usnmp_pdu_encode(pdu, &resp_b) != 0) {
		/* TODO gestion erreur */
		/*syslog(LOG_ERR, "cannot encode message");
		 abort();*/
		return -1;
	}
	*sndlen = (size_t) (resp_b.asn_ptr - sndbuf);
	return 0;
}

u_int32_t usnmp_next_reqid(usnmp_device_t dev) {
	static int reqid = 0;
	return ++reqid;
}
/* lock the socket before use usnmp_send_packet_pdu,usnmp_recv_packet_pdu if multithread
 * use the same usnmp_socket for sending and receiving */
/* simple pdu send, socket can't be null, dev too*/
u_int32_t usnmp_send_pdu(usnmp_pdu_t *pdu, usnmp_socket_t *psocket,
		usnmp_device_t dev) {
	/* snmp_output */
	u_int32_t reqid = usnmp_next_reqid(dev);
	if (USNMP_AUTO_REQID==pdu->request_id) {
		pdu->request_id = reqid;
	}
	/* set the community */
	if (pdu->type != USNMP_PDU_SET)
		strncpy(pdu->community, dev.public, sizeof(pdu->community));
	else
		strncpy(pdu->community, dev.private, sizeof(pdu->community));
	u_char *sndbuf = malloc(USNMP_MAX_MSG_SIZE);
	size_t sndlen;
	ssize_t len;
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_port = htons(dev.remote_port);
	addr.sin_addr = dev.ipv4;
	addr.sin_family = AF_INET;
	usnmp_build_packet(pdu, sndbuf, &sndlen);
	// TODO debug
	usnmp_fprintf_pdu_t(stdout,*pdu);
	if ((len = sendto(psocket->fd, sndbuf, sndlen, 0,
			(struct sockaddr *) &addr, sizeof(struct sockaddr_in))) == -1) {
		/*syslog(LOG_ERR, "sendto: %m");*/
		// TODO err
		perror("sendto : ");
	} else if ((size_t) len != sndlen) {
		/*syslog(LOG_ERR, "sendto: short write %zu/%zu", sndlen, (size_t) len);*/
		perror("sendto : short write ");
	}
	free(sndbuf);
	return reqid;
}
/* if timeout equal NULL, wait indefinitely
 * if timeout expire function or error return a negative value */
int usnmp_recv_pdu(usnmp_pdu_t ** retpdu, struct timeval * timeout,
		usnmp_socket_t *psocket) {
	u_char *resbuf = NULL;
	if (NULL==retpdu) {
		// TODO erro
		return -1;
	}
	ssize_t len;
	int32_t vi;
	struct sockaddr *ret;
	socklen_t *retlen;
	fd_set rfds;
	int sret = 0;
	int err = 0;

	if (NULL!=timeout) {
		struct timeval tv = *timeout;
		FD_ZERO(&rfds);
		FD_SET(psocket->fd, &rfds);
		sret = select(psocket->fd + 1, &rfds, NULL,NULL,&tv);
		/* TODO gest erreur */
		if ( -1==sret ) {
			perror("select()");
			return -1;
		} else if (0==sret) {
			/* FD_ISSET(socket+1, &rfds) est alors faux */
			return -2;
		}
	}

	if((resbuf = malloc(USNMP_MAX_MSG_SIZE))==NULL) {
		/* probleme d'allocation memoire */
		err=-1;
	} else if ((len = recvfrom(psocket->fd, resbuf, USNMP_MAX_MSG_SIZE, 0, ret,retlen)) == -1) {
		/* message de longueur null */
		err=-1;
	} else if ((size_t) len == USNMP_MAX_MSG_SIZE) {
		/* packet trop grand */
		err=-1;
	} else if (NULL==(*retpdu = (usnmp_pdu_t*) calloc(1, sizeof(usnmp_pdu_t)))){
		/* plus de memoire */
		err=-1;
	} else {
		/*
		 * Handle input
		 */
		struct asn_buf b;
		memset(&b,0,sizeof(struct asn_buf));
		b.asn_ptr = resbuf;
		b.asn_len = len;
		enum usnmp_code code = usnmp_pdu_decode(&b, *retpdu, &vi);
		switch (code) {
		case USNMP_CODE_FAILED:
		case USNMP_CODE_BADVERS:
		case USNMP_CODE_BADLEN:
		case USNMP_CODE_OORANGE:
		case USNMP_CODE_BADENC:
			/* INPUT ERROR PACKET MALFORMED */
			err=-1;
			break;
		case USNMP_CODE_OK:
			switch ((*retpdu)->version) {
			case USNMP_V1:
			case USNMP_V2c:
				/* ok do nothing */
				break;
			case USNMP_Verr:
			default:
				/* unknown version*/
				err=-2;
				break;
			}
			break;
		}
		if(0!= err){
			/* TODO error */
			fprintf(stderr,"bad pdu ...\n");
			usnmp_clean_pdu(*retpdu);
		}
	}
	if(resbuf!=NULL)free(resbuf);
	/**/
	return err;
}

/* return a malloc'd socket */
usnmp_socket_t * usnmp_create_and_open_socket(int port, struct timeval *tout) {
	usnmp_socket_t * usnmp_socket = (usnmp_socket_t *) calloc(1,
			sizeof(usnmp_socket_t));
	if (NULL==usnmp_socket) {
		perror("socket malloc fail !");
		return NULL;
	}
	pthread_mutex_init(&usnmp_socket->lockme, NULL);
	usnmp_socket->fd = socket(PF_INET,SOCK_DGRAM, 0);
	if (usnmp_socket->fd < 0) {
		/* ERROR */
		perror("socket open error");
		free(usnmp_socket);
		usnmp_socket = NULL;
	} else {
		int i = 0;
		bool quit = false;
		while (!quit) {
			if (port < 0) {
				/* random port between 2000 and 65535 */
				int irand = rand();
				port = (65535 - 2000) * (irand * 1.0 / RAND_MAX) + 2000;
			}
			usnmp_socket->sa_in.sin_family = AF_INET;
			usnmp_socket->sa_in.sin_port = htons(port);
			usnmp_socket->sa_in.sin_addr.s_addr = htonl(INADDR_ANY);
			if (bind(usnmp_socket->fd,
					(const struct sockaddr *) &usnmp_socket->sa_in,
					sizeof(usnmp_socket->sa_in))) {
				if (i > 5) {
					perror("bind error !");
					close(usnmp_socket->fd);
					free(usnmp_socket);
					usnmp_socket = NULL;
					quit = true;
				}
				i++;
			} else {
				quit = true;
			}
		}
	}
	/* socket is open and ready listen sending */
	return usnmp_socket;
}

/* close the UDP socket */
void usnmp_close_socket(usnmp_socket_t * psocket) {
	pthread_mutex_trylock(&psocket->lockme);
	pthread_mutex_unlock(&psocket->lockme);
	//pthread_mutex_destroy(&psocket->lockme);
	close(psocket->fd);
}

/* display function */
void usnmp_fprintf_device_t(FILE* _stream, usnmp_device_t dev) {
	// char buf[MAX_IPV4_LEN];
	fprintf(_stream, "Device : \n");
	fprintf(_stream, "\tipv4:[%s] \n", inet_ntoa(dev.ipv4));
	//fprintf(_stream,"\tipv4:[%s] \n" ,inet_neta(dev.ipv4,buf,MAX_IPV4_LEN));
	if (dev.remote_port > 0) {
		fprintf(_stream, "\tport:[%i] \n", dev.remote_port);
	} else {
		fprintf(_stream, "\tport:[%i] \n", USNMP_DEFAULT_SERV_PORT);
	}
	if (NULL==dev.public) {
		fprintf(_stream, "\tRead Community :[%s]\n",
				USNMP_DEFAULT_READ_COMMUNITY);
	} else {
		fprintf(_stream, "\tRead Community :[%s]\n", dev.public);
	}
	if (NULL==dev.private) {
		fprintf(_stream, "\tWrite Community :[%s]\n",
				USNMP_DEFAULT_WRITE_COMMUNITY);
	} else {
		fprintf(_stream, "\tWrite Community :[%s]\n", dev.private);
	}
}
void usnmp_fprintf_pdu_t(FILE* _stream, usnmp_pdu_t pdu) {
	int i = 0;
	char buf[ASN_OIDSTRLEN];
	const char *vers;
	static const char *types[] = { /**/
	[USNMP_PDU_GET] = "GET",/**/
	[USNMP_PDU_GETNEXT] = "GETNEXT", /**/
	[USNMP_PDU_RESPONSE] = "RESPONSE",/**/
	[USNMP_PDU_SET] = "SET",/**/
	[USNMP_PDU_TRAP] = "TRAPv1",/**/
	[USNMP_PDU_GETBULK] = "GETBULK", [USNMP_PDU_INFORM] = "INFORM",/**/
	[USNMP_PDU_TRAP2] = "TRAPv2", /**/
	[USNMP_PDU_REPORT] = "REPORT", };/**/

	if (pdu.version == USNMP_V1)
		vers = "SNMPv1";
	else if (pdu.version == USNMP_V2c)
		vers = "SNMPv2c";
	else
		vers = "v?";

	switch (pdu.type) {
	case USNMP_PDU_TRAP:
		fprintf(_stream, "%s %s '%s'\n", types[pdu.type], vers, pdu.community);
		fprintf(_stream, " enterprise=%s\n",
				asn_oid2str_r(&pdu.enterprise, buf));
		fprintf(_stream, " agent_addr=%u.%u.%u.%u\n", pdu.agent_addr[0],
				pdu.agent_addr[1], pdu.agent_addr[2], pdu.agent_addr[3]);
		fprintf(_stream, " generic_trap=%d\n", pdu.generic_trap);
		fprintf(_stream, " specific_trap=%d\n", pdu.specific_trap);
		fprintf(_stream, " time-stamp=%u\n", pdu.time_stamp);
		for (i = 0; i < pdu.nbindings; i++) {
			fprintf(_stream, " [%u]: ", i);
			usnmp_fprintf_binding(_stream, &pdu.bindings[i]);
			fprintf(_stream, "\n");
		}
		break;

	case USNMP_PDU_GET:
	case USNMP_PDU_GETNEXT:
	case USNMP_PDU_RESPONSE:
	case USNMP_PDU_SET:
	case USNMP_PDU_GETBULK:
	case USNMP_PDU_INFORM:
	case USNMP_PDU_TRAP2:
	case USNMP_PDU_REPORT:
		fprintf(_stream, "%s %s '%s'", types[pdu.type], vers, pdu.community);
		fprintf(_stream, " request_id=%d\n", pdu.request_id);
		fprintf(_stream, " error_status=%d\n", pdu.error_status);
		fprintf(_stream, " error_index=%d\n", pdu.error_index);
		for (i = 0; i < pdu.nbindings; i++) {
			fprintf(_stream, " [%u]: ", i);
			usnmp_fprintf_binding(_stream, &pdu.bindings[i]);
			fprintf(_stream, "\n");
		}
		break;

	default:
		fprintf(_stream, "bad pdu type %u\n", pdu.type);
		break;
	}
}

void usnmp_fprintf_binding(FILE* _stream, const usnmp_var_t *b) {
	u_int i;
	char buf[ASN_OIDSTRLEN];

	fprintf(_stream, "%s=", asn_oid2str_r(&b->var, buf));
	switch (b->syntax) {

	case USNMP_SYNTAX_NULL:
		fprintf(_stream, "NULL");
		break;

	case USNMP_SYNTAX_INTEGER:
		fprintf(_stream, "INTEGER %d", b->v.integer);
		break;

	case USNMP_SYNTAX_OCTETSTRING:
		fprintf(_stream, "OCTET STRING %ui:", b->v.octetstring.len);
		for (i = 0; i < b->v.octetstring.len; i++)
			fprintf(_stream, " %02x", b->v.octetstring.octets[i]);
		break;

	case USNMP_SYNTAX_OID:
		fprintf(_stream, "OID %s", asn_oid2str_r(&b->v.oid, buf));
		break;

	case USNMP_SYNTAX_IPADDRESS:
		fprintf(_stream, "IPADDRESS %u.%u.%u.%u", b->v.ipaddress[0],
				b->v.ipaddress[1], b->v.ipaddress[2], b->v.ipaddress[3]);
		break;

	case USNMP_SYNTAX_COUNTER:
		fprintf(_stream, "COUNTER %u", b->v.uint32);
		break;

	case USNMP_SYNTAX_GAUGE:
		fprintf(_stream, "GAUGE %u", b->v.uint32);
		break;

	case USNMP_SYNTAX_TIMETICKS:
		fprintf(_stream, "TIMETICKS %u", b->v.uint32);
		break;

	case USNMP_SYNTAX_COUNTER64:
		fprintf(_stream, "COUNTER64 %lld", b->v.counter64);
		break;

	case USNMP_SYNTAX_NOSUCHOBJECT:
		fprintf(_stream, "NoSuchObject");
		break;

	case USNMP_SYNTAX_NOSUCHINSTANCE:
		fprintf(_stream, "NoSuchInstance");
		break;

	case USNMP_SYNTAX_ENDOFMIBVIEW:
		fprintf(_stream, "EndOfMibView");
		break;

	default:
		fprintf(_stream, "UNKNOWN SYNTAX %u", b->syntax);
		break;
	}
}

void usmmp_fprintf_oid_t(FILE* _stream, usnmp_oid_t oid) {

}
