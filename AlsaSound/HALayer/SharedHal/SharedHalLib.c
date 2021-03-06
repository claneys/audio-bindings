/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author Fulup Ar Foll <fulup@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * reference: 
 *   amixer contents; amixer controls;
 *   http://www.tldp.org/HOWTO/Alsa-sound-6.html 
 */
#define _GNU_SOURCE  // needed for vasprintf
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>

#include "SharedHalLib.h"

typedef struct {
    int index;
    int numid;
} shareHallMap_T;

static struct afb_service srvitf;
static const struct afb_binding_interface *afbIface;
static shareHallMap_T *shareHallMap;

// Force specific HAL to depend on ShareHalLib
PUBLIC char* SharedHalLibVersion="1.0";

// This callback when api/alsacore/subscribe returns
STATIC void alsaSubcribeCB(void *handle, int iserror, struct json_object *result) {
    struct afb_req request = afb_req_unstore(handle);
    struct json_object *x, *resp = NULL;
    const char *info = NULL;

    if (result) {
        INFO(afbIface, "result=[%s]\n", json_object_to_json_string(result));
        if (json_object_object_get_ex(result, "request", &x) && json_object_object_get_ex(x, "info", &x))
            info = json_object_get_string(x);
        if (!json_object_object_get_ex(result, "response", &resp)) resp = NULL;
    }

    // push message respond
    if (iserror) afb_req_fail_f(request, "Fail", info);
    else afb_req_success(request, resp, info);

    // free calling request
    afb_req_unref(request);
}

// Create and subscribe to alsacore ctl events

STATIC void halMonitor(struct afb_req request) {

    // save request in session as it might be used after return by callback
    struct afb_req *handle = afb_req_store(request);

    // push request to low level binding
    if (!handle) afb_req_fail(request, "error", "out of memory");
    else afb_service_call(srvitf, "alsacore", "subctl", json_object_get(afb_req_json(request)), alsaSubcribeCB, handle);

    // success/failure messages return from callback    
}

// Subscribe to AudioBinding events

STATIC void halSubscribe(struct afb_req request) {
    const char *devid = afb_req_value(request, "devid");
    if (devid == NULL) {
        afb_req_fail_f(request, "devid-missing", "devid=hw:xxx missing");
    }
}

// Call when all bindings are loaded and ready to accept request

STATIC void halGetVol(struct afb_req request) {

    // Should call here Hardware Alsa Abstraction Layer for corresponding Sound Card
    afb_req_success(request, NULL, NULL);
    return;

}

STATIC void halSetVol(struct afb_req request) {
    const char *arg;
    const char *pcm;

    arg = afb_req_value(request, "vol");
    if (arg == NULL) {
        afb_req_fail_f(request, "argument-missing", "vol=[0,100] missing");
        goto OnErrorExit;
    }

    pcm = afb_req_value(request, "pcm");
    if (pcm == NULL) pcm = "Master";

    // Should call here Hardware Alsa Abstraction Layer for corresponding Sound Card
    afb_req_success(request, NULL, NULL);
    return;

OnErrorExit:
    return;

}

// AudioLogic expect volume values to be 0-100%

STATIC int NormaliseValue(const alsaHalCtlMapT *halCtls, int valuein) {
    int valueout;

    if (valuein > halCtls->maxval) valuein= halCtls->maxval;
    if (valuein < halCtls->minval) valuein= halCtls->minval;

    // volume are ajusted to 100% any else is ignored
    switch (halCtls->group) {
        case OUTVOL:
        case PCMVOL:
        case INVOL:
            valueout = (valuein * 100) / (halCtls->maxval - halCtls->minval);
            break;
        default:
            valueout = valuein;
    }
    return (valueout);
}

// receive controls for LowLevel remap them to hight level before returning them

STATIC void halGetControlCB(void *handle, int iserror, struct json_object *result) {
    alsaHalMapT *halCtls = alsaHalSndCard.ctls;
    struct json_object *response;

    // retrieve request and check for errors
    struct afb_req request = afb_req_unstore(handle);
    if (!cbCheckResponse(request, iserror, result)) goto OnExit;

        // Get response from object
        json_object_object_get_ex(result, "response", &response);
    if (!response) {
        afb_req_fail_f(request, "response-notfound", "No Controls return from alsa/getcontrol result=[%s]", json_object_get_string(result));
        goto OnExit;
    }

    // extract sounds controls information from received Object 
    struct json_object *ctls;
    json_object_object_get_ex(result, "ctls", &ctls);
    if (!ctls) {
        afb_req_fail_f(request, "ctls-notfound", "No Controls return from alsa/getcontrol response=[%s]", json_object_get_string(response));
        goto OnExit;
    }

    // make sure return controls have a valid type
    if (json_object_get_type(ctls) != json_type_array) {
        afb_req_fail_f(request, "ctls-notarray", "Invalid Controls return from alsa/getcontrol response=[%s]", json_object_get_string(response));
        goto OnExit;
    }

    // loop on array and store values into client context
    for (int idx = 0; idx < json_object_array_length(ctls); idx++) {
        struct json_object *ctl;
        int rawvalue, numid;

        ctl = json_object_array_get_idx(ctls, idx);
        if (json_object_array_length(ctl) != 2) {
            afb_req_fail_f(request, "ctl-invalid", "Invalid Control return from alsa/getcontrol ctl=[%s]", json_object_get_string(ctl));
            goto OnExit;
        }

        // As HAL and Business logic use the same AlsaMixerHal.h direct conversion is not an issue
        numid = (halCtlsEnumT) json_object_get_int(json_object_array_get_idx(ctl, 0));
        rawvalue = json_object_get_int(json_object_array_get_idx(ctl, 1));

        for (int idx = 0; halCtls[idx].alsa.numid != 0; idx++) {
            if (halCtls[idx].alsa.numid == numid) {
                struct json_object *ctlAndValue = json_object_new_array();
                json_object_array_add(ctlAndValue, json_object_new_int(idx)); // idx == high level control
                json_object_array_add(ctlAndValue, json_object_new_int(NormaliseValue(&halCtls[idx].alsa, rawvalue)));
                break;
            }
        }
        if (shareHallMap[idx].numid == 0) {

            afb_req_fail_f(request, "ctl-invalid", "Invalid Control numid from alsa/getcontrol ctl=[%s]", json_object_get_string(ctl));
                    goto OnExit;
        }
    }

    OnExit:
            return;
}


// Translate high level control to low level and call lower layer
STATIC void halGetCtls(struct afb_req request) {

    struct json_object *queryin, *queryout, *ctlsin, *devid;
    struct json_object *ctlsout = json_object_new_array();

    // get query from request
    queryin = afb_req_json(request);

    // check devid was given
    if (!json_object_object_get_ex (queryin, "devid", &devid)) {
        afb_req_fail_f(request, "devid-notfound", "No DevID given query=[%s]", json_object_get_string(queryin));
                goto OnExit;
    }

    // loop on requested controls
    if (!json_object_object_get_ex(queryin, "ctls", &ctlsin) || json_object_array_length(ctlsin) <= 0) {
        afb_req_fail_f(request, "ctlsin-notfound", "No Controls given query=[%s]", json_object_get_string(queryin));
                goto OnExit;
    }

    // loop on controls 
    for (int idx = 0; idx < json_object_array_length(ctlsin); idx++) {
        struct json_object *ctl;
        halCtlsEnumT control;

        // each control should be halCtlsEnumT
        ctl = json_object_array_get_idx(ctlsin, idx);
        control = (halCtlsEnumT) json_object_get_int(ctl);
        if (control >= EndHalCrlTag || control <= StartHalCrlTag) {
            afb_req_fail_f(request, "ctl-invalid", "Invalid Control devid=%s sndcard=%s ctl=[%s] should be [%d-%d]"
                    , json_object_get_string(devid), alsaHalSndCard.name, json_object_get_string(ctl), StartHalCrlTag, EndHalCrlTag);
            goto OnExit;
        }

        // add corresponding lowlevel numid to querylist
        json_object_array_add(ctlsout, json_object_new_int(shareHallMap[control].numid));
    }

    // register HAL with Alsa Low Level Binder devid=hw:0&numid=1&quiet=0
    queryout = json_object_new_object();
    json_object_object_add(queryout, "devid", devid);
    json_object_object_add(queryout, "ctls", ctlsout);

    // save request context dans call lowlevel API
    struct afb_req *handle = afb_req_store(request);
    afb_service_call(srvitf, "alsacore", "getctl", queryout, halGetControlCB, handle);

OnExit:
    // Under normal situation success/failure is set from callback
    return;
};

STATIC void halInitCB(void *handle, int iserror, struct json_object *result) {
    struct json_object *queryurl = (json_object*) handle;

    if (iserror) NOTICE(afbIface, "halInitCB: registration alsaHAL query=[%s] Fail", json_object_to_json_string(queryurl));
    else DEBUG(afbIface, "halInitCB: registration alsaHAL card=[%s] Success", json_object_to_json_string(queryurl));
}

// This receive all event this binding subscribe to 
PUBLIC void afbBindingV1ServiceEvent(const char *evtname, struct json_object *object) {

    NOTICE(afbIface, "afbBindingV1ServiceEvent evtname=%s [msg=%s]", evtname, json_object_to_json_string(object));
}

// this is call when after all bindings are loaded
PUBLIC int afbBindingV1ServiceInit(struct afb_service service) {
    int rc=0;
    srvitf = service;
    struct json_object *queryurl;
    
    if (alsaHalSndCard.initCB) {
        rc= (alsaHalSndCard.initCB) (afbIface, service);
        if (rc != 0) goto OnExit;
    }

    // register HAL with Alsa Low Level Binder
    queryurl = json_object_new_object();
    json_object_object_add(queryurl, "prefix", json_object_new_string(alsaHalSndCard.prefix));
    json_object_object_add(queryurl, "name", json_object_new_string(alsaHalSndCard.name));
    afb_service_call(srvitf, "alsacore", "registerHal", queryurl, halInitCB, queryurl);

  OnExit:
    return (rc);
};

// Every HAL export the same API & Interface Mapping from SndCard to AudioLogic is done through alsaHalSndCardT
static const struct afb_verb_desc_v1 halSharedApi[] = {
    /* VERB'S NAME            SESSION MANAGEMENT          FUNCTION TO CALL         SHORT DESCRIPTION */
    { .name = "ping",      .session = AFB_SESSION_NONE, .callback = pingtest,   .info = "Ping Binding"},
    { .name = "getctls",   .session = AFB_SESSION_NONE, .callback = halGetCtls,  .info = "Get Control"},
    { .name = "setvol",    .session = AFB_SESSION_NONE, .callback = halSetVol,   .info = "Set Volume"},
    { .name = "getvol",    .session = AFB_SESSION_NONE, .callback = halGetVol,   .info = "Get Volume"},
    { .name = "subscribe", .session = AFB_SESSION_NONE, .callback = halSubscribe,.info = "Subscribe Alsa Events"},
    { .name = "monitor",   .session = AFB_SESSION_NONE, .callback = halMonitor  ,.info = "Monitor Alsa Events"},
    { .name = NULL} /* marker for end of the array */
};


static struct afb_binding alsaHalBinding = {
  /* description conforms to VERSION 1 */
 .type= AFB_BINDING_VERSION_1,
 .v1= {
    .prefix= NULL,
    .info  = NULL,
    .verbs = halSharedApi,
 }
};

// Process HAL mapping from alsaHalSndCardT before registering HAL binder
PUBLIC const struct afb_binding *afbBindingV1Register(const struct afb_binding_interface *itf)  {
    
    afbIface = itf; // need to keep a static trace of binder interface for avances functions
    // alsaHalBinding.v1.verbs = halSharedApi;  // complete sndcard specific alsaHalBinding with standard HAL APIs
    alsaHalMapT *halCtls = alsaHalSndCard.ctls; // Get sndcard specific HAL control mapping

    if (halCtls == NULL) {
        ERROR(afbIface, "afbBindingV1Register Fail alsaHalCtlsMap==NULL");
        return NULL;
    }

    // Create a zone to store HAL high/low level mapping
    shareHallMap = malloc(EndHalCrlTag * sizeof (shareHallMap_T));
    for (int idx = 0; (halCtls[idx].alsa.numid != 0 || halCtls[idx].cb.callback != NULL); idx++) {
        if (halCtls[idx].alsa.numid == 0) halCtls[idx].alsa.numid =-1; 
        shareHallMap[halCtls[idx].alsa.control].numid = halCtls[idx].alsa.numid;
    }
    
    alsaHalBinding.v1.prefix= alsaHalSndCard.prefix;
    alsaHalBinding.v1.info  = alsaHalSndCard.info;     
    return &alsaHalBinding; /* returns the description of the binding */
};
