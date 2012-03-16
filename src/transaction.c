/*
	belle-sip - SIP (RFC3261) library.
    Copyright (C) 2010  Belledonne Communications SARL

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "belle_sip_internal.h"

static void belle_sip_transaction_init(belle_sip_transaction_t *t, belle_sip_provider_t *prov, belle_sip_request_t *req){
	t->request=(belle_sip_request_t*)belle_sip_object_ref(req);
	t->provider=prov;
}

static void transaction_destroy(belle_sip_transaction_t *t){
	if (t->request) belle_sip_object_unref(t->request);
	if (t->prov_response) belle_sip_object_unref(t->prov_response);
	if (t->final_response) belle_sip_object_unref(t->final_response);
	if (t->channel) belle_sip_object_unref(t->channel);
}

BELLE_SIP_DECLARE_NO_IMPLEMENTED_INTERFACES(belle_sip_transaction_t);

BELLE_SIP_INSTANCIATE_CUSTOM_VPTR(belle_sip_transaction_t)={
	{
		BELLE_SIP_VPTR_INIT(belle_sip_transaction_t,belle_sip_object_t,FALSE),
		(belle_sip_object_destroy_t) transaction_destroy,
		NULL,/*no clone*/
		NULL,/*no marshall*/
	},
	NULL /*on_terminate*/
};

void *belle_sip_transaction_get_application_data(const belle_sip_transaction_t *t){
	return t->appdata;
}

void belle_sip_transaction_set_application_data(belle_sip_transaction_t *t, void *data){
	t->appdata=data;
}

const char *belle_sip_transaction_get_branch_id(const belle_sip_transaction_t *t){
	return t->branch_id;
}

belle_sip_transaction_state_t belle_sip_transaction_get_state(const belle_sip_transaction_t *t){
	return t->state;
}

void belle_sip_transaction_terminate(belle_sip_transaction_t *t){
	belle_sip_transaction_terminated_event_t ev;
	
	t->state=BELLE_SIP_TRANSACTION_TERMINATED;
	belle_sip_provider_set_transaction_terminated(t->provider,t);
	BELLE_SIP_OBJECT_VPTR(t,belle_sip_transaction_t)->on_terminate(t);
	
	ev.source=t->provider;
	ev.transaction=t;
	ev.is_server_transaction=BELLE_SIP_IS_INSTANCE_OF(t,belle_sip_server_transaction_t);
	BELLE_SIP_PROVIDER_INVOKE_LISTENERS(t->provider,process_transaction_terminated,&ev);
}

belle_sip_request_t *belle_sip_transaction_get_request(belle_sip_transaction_t *t){
	return t->request;
}

void belle_sip_transaction_notify_timeout(belle_sip_transaction_t *t){
	belle_sip_timeout_event_t ev;
	ev.source=t->provider;
	ev.transaction=t;
	ev.is_server_transaction=BELLE_SIP_OBJECT_IS_INSTANCE_OF(t,belle_sip_server_transaction_t);
	BELLE_SIP_PROVIDER_INVOKE_LISTENERS(t->provider,process_timeout,&ev);
}

/*
 * Server transaction
 */

static void server_transaction_destroy(belle_sip_server_transaction_t *t){
}


BELLE_SIP_DECLARE_NO_IMPLEMENTED_INTERFACES(belle_sip_server_transaction_t);
BELLE_SIP_INSTANCIATE_CUSTOM_VPTR(belle_sip_server_transaction_t)={
	{
		{
			BELLE_SIP_VPTR_INIT(belle_sip_server_transaction_t,belle_sip_transaction_t,FALSE),
			(belle_sip_object_destroy_t) server_transaction_destroy,
			NULL,
			NULL
		},
		NULL
	}
};

belle_sip_server_transaction_t * belle_sip_server_transaction_new(belle_sip_provider_t *prov,belle_sip_request_t *req){
	belle_sip_server_transaction_t *t=belle_sip_object_new(belle_sip_server_transaction_t);
	belle_sip_transaction_init((belle_sip_transaction_t*)t,prov,req);
	return t;
}

void belle_sip_server_transaction_send_response(belle_sip_server_transaction_t *t, belle_sip_response_t *resp){
	//BELLE_SIP_OBJECT_VPTR(t,belle_sip_transaction_t)->on_response((belle_sip_transaction_t*)t,resp);
}

/*
 * client transaction
 */


belle_sip_request_t * belle_sip_client_transaction_create_cancel(belle_sip_client_transaction_t *t){
	return NULL;
}


void belle_sip_client_transaction_send_request(belle_sip_client_transaction_t *t){
	belle_sip_hop_t hop={0};
	belle_sip_channel_t *chan;
	belle_sip_provider_t *prov=t->base.provider;
	
	if (t->base.state!=BELLE_SIP_TRANSACTION_INIT){
		belle_sip_error("belle_sip_client_transaction_send_request: bad state.");
		return;
	}
	belle_sip_stack_get_next_hop(prov->stack,t->base.request,&hop);
	chan=belle_sip_provider_get_channel(prov,hop.host, hop.port, hop.transport);
	if (chan){
		belle_sip_object_ref(chan);
		belle_sip_channel_add_listener(chan,BELLE_SIP_CHANNEL_LISTENER(t));
		t->base.channel=chan;
		if (belle_sip_channel_get_state(chan)==BELLE_SIP_CHANNEL_INIT)
			belle_sip_channel_prepare(chan);
		if (belle_sip_channel_get_state(chan)!=BELLE_SIP_CHANNEL_READY){
			belle_sip_message("belle_sip_client_transaction_send_request(): waiting channel to be ready");
		}
	}else belle_sip_error("belle_sip_client_transaction_send_request(): no channel available");
}

void belle_sip_client_transaction_add_response(belle_sip_client_transaction_t *t, belle_sip_response_t *resp){
	belle_sip_transaction_t *base=(belle_sip_transaction_t*)t;
	int pass=BELLE_SIP_OBJECT_VPTR(t,belle_sip_client_transaction_t)->on_response(t,resp);
	if (pass){
		belle_sip_response_event_t ev;

		if (base->prov_response)
			belle_sip_object_unref(base->prov_response);
		base->prov_response=(belle_sip_response_t*)belle_sip_object_ref(resp);
		
		ev.source=base->provider;
		ev.response=resp;
		ev.client_transaction=t;
		ev.dialog=NULL;
		BELLE_SIP_PROVIDER_INVOKE_LISTENERS(base->provider,process_response_event,&ev);
	}
}

static void client_transaction_destroy(belle_sip_client_transaction_t *t ){
}

static void on_channel_state_changed(belle_sip_channel_listener_t *l, belle_sip_channel_t *chan, belle_sip_channel_state_t state){
	belle_sip_client_transaction_t *t=(belle_sip_client_transaction_t*)l;
	belle_sip_message("transaction on_channel_state_changed");
	switch(state){
		case BELLE_SIP_CHANNEL_READY:
			belle_sip_provider_add_client_transaction(t->base.provider,t);
			BELLE_SIP_OBJECT_VPTR(t,belle_sip_client_transaction_t)->send_request(t);
		break;
		default:
			/*ignored*/
		break;
	}
}

BELLE_SIP_IMPLEMENT_INTERFACE_BEGIN(belle_sip_client_transaction_t,belle_sip_channel_listener_t)
on_channel_state_changed,
NULL,
NULL
BELLE_SIP_IMPLEMENT_INTERFACE_END

BELLE_SIP_DECLARE_IMPLEMENTED_INTERFACES_1(belle_sip_client_transaction_t, belle_sip_channel_listener_t);
BELLE_SIP_INSTANCIATE_CUSTOM_VPTR(belle_sip_client_transaction_t)={
	{
		{
			BELLE_SIP_VPTR_INIT(belle_sip_client_transaction_t,belle_sip_transaction_t,FALSE),
			(belle_sip_object_destroy_t)client_transaction_destroy,
			NULL,
			NULL
		},
		NULL
	},
	NULL,
	NULL
};

void belle_sip_client_transaction_init(belle_sip_client_transaction_t *obj, belle_sip_provider_t *prov, belle_sip_request_t *req){
	belle_sip_header_via_t *via=BELLE_SIP_HEADER_VIA(belle_sip_message_get_header((belle_sip_message_t*)req,"via"));
	char token[10];
	obj->base.branch_id=belle_sip_strdup_printf(BELLE_SIP_BRANCH_MAGIC_COOKIE ".%s",belle_sip_random_token(token,sizeof(token)));
	if (via){
		belle_sip_header_via_set_branch(via,obj->base.branch_id);
	}else{
		belle_sip_fatal("belle_sip_client_transaction_init(): No via in request.");
	}
	belle_sip_transaction_init((belle_sip_transaction_t*)obj, prov,req);
}

belle_sip_ist_t *belle_sip_ist_new(belle_sip_provider_t *prov, belle_sip_request_t *req){
	return NULL;
}

belle_sip_nist_t *belle_sip_nist_new(belle_sip_provider_t *prov, belle_sip_request_t *req){
	return NULL;
}

