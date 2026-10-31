/* PSN libraries for the offline console (runtime_services.c holds NpManager, the older NpScore,
 * Matching2 and WebApi calls, and the trophies). The one user is signed out, so (shadPS4 np_*):
 *   - creating a title, user or matching context fails with NP SIGNED_OUT;
 *   - a call on a context or request that can therefore not exist fails with that library's
 *     INVALID_ID; a request object the old API created fails its transfer with SIGNED_OUT;
 *   - objects that need no sign-in (WebApi handles and push filters, InGameMessage contexts,
 *     NpAuth requests) are created, and their server calls fail with SIGNED_OUT.
 * Dialogs about PSN or the system (sign-in, invitation, error, social screen) draw nothing: an
 * opened one finishes at once, cancelled by the user. */
#include "runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#define NP_SIGNED_OUT ((int32_t)0x80550006)
#define COMMUNITY_INVALID_ARGUMENT ((int32_t)0x80550704)
#define COMMUNITY_INVALID_ID ((int32_t)0x8055070E)
#define AUTH_INVALID_ARGUMENT ((int32_t)0x80550301)
#define MATCHING2_INVALID_ARGUMENT ((int32_t)0x80550C0A)
#define MATCHING2_INVALID_CONTEXT_ID ((int32_t)0x80550C0B)
#define WEBAPI_INVALID_ARGUMENT ((int32_t)0x80552902)
#define WEBAPI_INVALID_LIB_CONTEXT_ID ((int32_t)0x80552903)
#define INVALID_SERVICE_LABEL 0xFFFFFFFFu
#define USER_INVALID (-1)

static _Atomic int32_t next_object=1;
static int32_t new_object(void) { return atomic_fetch_add(&next_object,1); }
static ABI int32_t ok(void) { return 0; }
static ABI int32_t signed_out(void) { return NP_SIGNED_OUT; }

/* ---- NpTus / NpTss (shadPS4 np_tus.cpp) ----
 * sceNpTusCreateNpTitleCtxA(label, user) and sceNpTssCreateNpTitleCtxA need the user's NpId:
 * SIGNED_OUT. With no title context, sceNpTusCreateRequest(ctx), every Get/Set/Add/TryAndSet/
 * Delete call on a request, sceNpTusAbortRequest, sceNpTusDeleteRequest and
 * sceNpTusDeleteNpTitleCtx find no such id: COMMUNITY INVALID_ID. */
static ABI int32_t community_no_id(void) { return COMMUNITY_INVALID_ID; }

/* ---- NpScore A variants (shadPS4 np_score.cpp) ----
 * sceNpScoreCreateNpTitleCtxA(label, user): SIGNED_OUT first, then the label is checked. The
 * ranking and game-data calls take a request from sceNpScoreCreateRequest and fail SIGNED_OUT,
 * as the older calls in runtime_services.c do. */
static ABI int32_t score_ctx_a(uint32_t label,int32_t user) { (void)label; (void)user; return NP_SIGNED_OUT; }

/* ---- NpMatching2 (shadPS4 np_matching2.cpp) ----
 * sceNpMatching2CreateContextA(param{userId, serviceLabel, size=0x10}, ctxId): INVALID_ARGUMENT
 * for a bad param, else SIGNED_OUT. Every call that names a context then fails with
 * INVALID_CONTEXT_ID. sceNpMatching2GetMemoryInfo / GetSslMemoryInfo: OK, as shadPS4. */
static ABI int32_t matching2_context_a(const uint8_t *param,uint16_t *ctx) {
    if (!param || !ctx) return MATCHING2_INVALID_ARGUMENT;
    uint64_t size; memcpy(&size,param+8,8);
    return size!=0x10 ? MATCHING2_INVALID_ARGUMENT : NP_SIGNED_OUT;
}
static ABI int32_t matching2_no_context(void) { return MATCHING2_INVALID_CONTEXT_ID; }

/* ---- NpWebApi (shadPS4 np_web_api.cpp) ----
 * sceNpWebApiCreateContextA(libCtx, user): a user context needs PSN: SIGNED_OUT (as
 * sceNpWebApiCreateContext). Handles and extended push-event filters belong to the library
 * context and are created; requests and push callbacks need a user context: SIGNED_OUT. */
static ABI int32_t webapi_context_a(int32_t lib,int32_t user) {
    if (lib>=0x8000) return WEBAPI_INVALID_LIB_CONTEXT_ID;
    return user==USER_INVALID ? WEBAPI_INVALID_ARGUMENT : NP_SIGNED_OUT;
}
static ABI int32_t webapi_handle(int32_t lib) { return lib>=0x8000 ? WEBAPI_INVALID_LIB_CONTEXT_ID : new_object(); }
static ABI int32_t webapi_header(int64_t request,const char *name,const char *value) {
    (void)request; return !name || !value ? WEBAPI_INVALID_ARGUMENT : NP_SIGNED_OUT;
}
/* sceNpWebApiCreateExtdPushEventFilter(lib, handle, service name, label, params, count). */
static ABI int32_t webapi_filter(int32_t lib,int32_t handle,const char *service,uint32_t label,const void *params,uint64_t count) {
    (void)handle;
    if ((service && label==INVALID_SERVICE_LABEL) || !params || !count) return WEBAPI_INVALID_ARGUMENT;
    return lib>=0x8000 ? WEBAPI_INVALID_LIB_CONTEXT_ID : new_object();
}
/* sceNpWebApiRegisterExtdPushEventCallbackA(userCtx, filter, callback, arg). */
static ABI int32_t webapi_callback_a(int32_t ctx,int32_t filter,void *callback,void *arg) {
    (void)ctx; (void)filter; (void)arg; return callback ? NP_SIGNED_OUT : WEBAPI_INVALID_ARGUMENT;
}

/* ---- NpUtility (shadPS4 np_utility.cpp) ----
 * sceNpWordFilterCreateTitleCtxA(user): SIGNED_OUT; the request and sanitize calls then name a
 * context that does not exist: COMMUNITY INVALID_ID. The bandwidth test measures the PSN
 * connection: sceNpBandwidthTestInitStart / GetStatus / Shutdown fail SIGNED_OUT, Abort is OK. */

/* ---- NpManager InGameMessage ----
 * sceNpInGameMessageInitialize(pool size, option) gives a library context and
 * sceNpInGameMessageCreateHandle(ctx) a handle; PrepareA and SendDataA send to other players:
 * SIGNED_OUT. Delete, Terminate and the pool statistics are OK. */
static ABI int32_t object(void) { return new_object(); }

/* ---- NpAuth (shadPS4 np_auth.cpp) ----
 * sceNpAuthCreateRequest: a synchronous request id (0x10000001 up). sceNpAuthGetAuthorizationCodeA
 * (req, param, code, issuer) and sceNpAuthGetIdTokenA(req, param, token) need param and output,
 * then fail SIGNED_OUT. sceNpAuthAbortRequest: OK. */
static _Atomic int32_t next_auth=0x10000001;
static ABI int32_t auth_request(void) { return atomic_fetch_add(&next_auth,1); }
static ABI int32_t auth_code_a(int32_t req,const void *param,void *code,int32_t *issuer) {
    (void)req; (void)issuer; return !param || !code ? AUTH_INVALID_ARGUMENT : NP_SIGNED_OUT;
}
static ABI int32_t auth_token_a(int32_t req,const void *param,void *token) {
    (void)req; return !param || !token ? AUTH_INVALID_ARGUMENT : NP_SIGNED_OUT;
}

/* ---- NpCommerce: sceNpCommerceShowPsStoreIcon(pos) / HidePsStoreIcon: OK, no icon is drawn
 * (the Store is unreachable offline). ---- */

/* ---- Dialogs: status 0 none, 1 initialized, 2 running, 3 finished ---- */
#define DIALOG_NOT_INITIALIZED ((int32_t)0x80B80003)
#define DIALOG_ALREADY_INITIALIZED ((int32_t)0x80B80004)
#define DIALOG_NOT_FINISHED ((int32_t)0x80B80005)
#define DIALOG_ARG_NULL ((int32_t)0x80B8000D)
#define RESULT_USER_CANCELED 1
enum { SIGNIN, INVITATION, ERROR_DIALOG, SOCIAL_SCREEN, DIALOGS };
static const char *const dialog_names[DIALOGS]={"SigninDialog","InvitationDialog","ErrorDialog","SocialScreenDialog"};
static _Atomic int dialog_status[DIALOGS];
static int32_t dialog_init(int d) {
    int none=0;
    return atomic_compare_exchange_strong(&dialog_status[d],&none,1) ? 0 : DIALOG_ALREADY_INITIALIZED;
}
static int32_t dialog_open(int d,const char *detail) {
    if (!atomic_load(&dialog_status[d])) return DIALOG_NOT_INITIALIZED;
    printf("Runtime: %s opened%s; finished at once (offline, no dialog UI)\n",dialog_names[d],detail);
    atomic_store(&dialog_status[d],3); return 0;
}
static int32_t dialog_result(int d,void *result) {
    if (atomic_load(&dialog_status[d])!=3) return DIALOG_NOT_FINISHED;
    return result ? 0 : DIALOG_ARG_NULL;
}
static int32_t dialog_term(int d) {
    if (!atomic_load(&dialog_status[d])) return DIALOG_NOT_INITIALIZED;
    atomic_store(&dialog_status[d],0); return 0;
}
/* sceSigninDialog*: the player cannot sign in offline. Result {int32 result; reserved[32]}. */
static ABI int32_t signin_init(void) { return dialog_init(SIGNIN); }
static ABI int32_t signin_open(const void *param) { (void)param; return dialog_open(SIGNIN,""); }
static ABI int32_t signin_status(void) { return atomic_load(&dialog_status[SIGNIN]); }
static ABI int32_t signin_result(int32_t *result) {
    int32_t r=dialog_result(SIGNIN,result);
    if (!r) *result=RESULT_USER_CANCELED;
    return r;
}
static ABI int32_t signin_term(void) { return dialog_term(SIGNIN); }
/* sceInvitationDialog*: nothing can be sent. OrbisInvitationDialogResultA: callbackArg @0 (kept),
 * errorCode @8, result @12, sentUsers @16, reserved[32]. */
static ABI int32_t invitation_init(void) { return dialog_init(INVITATION); }
static ABI int32_t invitation_open(const void *param) { (void)param; return dialog_open(INVITATION,""); }
static ABI int32_t invitation_status(void) { return atomic_load(&dialog_status[INVITATION]); }
static ABI int32_t invitation_result(uint8_t *result) {
    int32_t r=dialog_result(INVITATION,result);
    if (r) return r;
    int32_t codes[2]={NP_SIGNED_OUT,RESULT_USER_CANCELED};
    memcpy(result+8,codes,8); memset(result+16,0,40);
    return 0;
}
static ABI int32_t invitation_term(void) { return dialog_term(INVITATION); }
/* sceErrorDialog*: param {int32 size, int32 errorCode, ...}; the code goes to the log. */
static ABI int32_t error_init(void) { return dialog_init(ERROR_DIALOG); }
static ABI int32_t error_open(const int32_t *param) {
    if (!param) return DIALOG_ARG_NULL;
    char detail[48]; snprintf(detail,sizeof(detail)," (error 0x%08x)",(uint32_t)param[1]);
    return dialog_open(ERROR_DIALOG,detail);
}
static ABI int32_t error_status(void) { return atomic_load(&dialog_status[ERROR_DIALOG]); }
static ABI int32_t error_term(void) { return dialog_term(ERROR_DIALOG); }
/* sceSocialScreenDialog*: no PS VR, so nothing to share. The result's first field is the result. */
static ABI int32_t social_init(void) { return dialog_init(SOCIAL_SCREEN); }
static ABI int32_t social_open(const void *param) { (void)param; return dialog_open(SOCIAL_SCREEN,""); }
static ABI int32_t social_status(void) { return atomic_load(&dialog_status[SOCIAL_SCREEN]); }
static ABI int32_t social_result(int32_t *result) {
    int32_t r=dialog_result(SOCIAL_SCREEN,result);
    if (!r) *result=RESULT_USER_CANCELED;
    return r;
}
/* sceSocialScreenDialogClose: the dialog has already finished; closing it is OK. */
static ABI int32_t social_close(void) { return atomic_load(&dialog_status[SOCIAL_SCREEN]) ? 0 : DIALOG_NOT_INITIALIZED; }
static ABI int32_t social_term(void) { return dialog_term(SOCIAL_SCREEN); }

static const RuntimeExport exports[]={
    {"sceNpTusCreateNpTitleCtxA",signed_out}, {"sceNpTssCreateNpTitleCtxA",signed_out},
    {"sceNpTusCreateRequest",community_no_id}, {"sceNpTusDeleteRequest",community_no_id},
    {"sceNpTusAbortRequest",community_no_id}, {"sceNpTusDeleteNpTitleCtx",community_no_id},
    {"sceNpTssGetData",community_no_id},
    {"sceNpTusGetDataA",community_no_id}, {"sceNpTusGetDataAVUser",community_no_id},
    {"sceNpTusGetDataForCrossSave",community_no_id}, {"sceNpTusGetDataForCrossSaveVUser",community_no_id},
    {"sceNpTusSetDataA",community_no_id}, {"sceNpTusSetDataAVUser",community_no_id},
    {"sceNpTusDeleteMultiSlotDataA",community_no_id}, {"sceNpTusDeleteMultiSlotDataVUser",community_no_id},
    {"sceNpTusAddAndGetVariableA",community_no_id}, {"sceNpTusAddAndGetVariableAVUser",community_no_id},
    {"sceNpTusAddAndGetVariableForCrossSave",community_no_id},
    {"sceNpTusAddAndGetVariableForCrossSaveVUser",community_no_id},
    {"sceNpTusTryAndSetVariableA",community_no_id}, {"sceNpTusTryAndSetVariableAVUser",community_no_id},
    {"sceNpTusTryAndSetVariableForCrossSave",community_no_id},
    {"sceNpTusTryAndSetVariableForCrossSaveVUser",community_no_id},
    {"sceNpTusSetMultiSlotVariableA",community_no_id}, {"sceNpTusSetMultiSlotVariableVUser",community_no_id},
    {"sceNpTusGetMultiSlotVariableA",community_no_id}, {"sceNpTusGetMultiSlotVariableAVUser",community_no_id},
    {"sceNpTusGetMultiSlotVariableForCrossSave",community_no_id},
    {"sceNpTusGetMultiSlotVariableForCrossSaveVUser",community_no_id},
    {"sceNpTusGetMultiUserVariableA",community_no_id}, {"sceNpTusGetMultiUserVariableAVUser",community_no_id},
    {"sceNpTusGetMultiUserVariableForCrossSave",community_no_id},
    {"sceNpTusGetMultiUserVariableForCrossSaveVUser",community_no_id},
    {"sceNpTusGetMultiUserDataStatusA",community_no_id}, {"sceNpTusGetMultiUserDataStatusAVUser",community_no_id},
    {"sceNpTusGetMultiUserDataStatusForCrossSave",community_no_id},
    {"sceNpTusGetMultiUserDataStatusForCrossSaveVUser",community_no_id},
    {"sceNpTusGetFriendsVariableA",community_no_id}, {"sceNpTusGetFriendsVariableForCrossSave",community_no_id},
    {"sceNpTusGetFriendsDataStatusA",community_no_id}, {"sceNpTusGetFriendsDataStatusForCrossSave",community_no_id},
    {"sceNpScoreCreateNpTitleCtxA",score_ctx_a},
    {"sceNpScoreGetRankingByRangeA",signed_out}, {"sceNpScoreGetRankingByRangeForCrossSave",signed_out},
    {"sceNpScoreGetRankingByAccountId",signed_out}, {"sceNpScoreGetRankingByAccountIdForCrossSave",signed_out},
    {"sceNpScoreGetRankingByAccountIdPcId",signed_out}, {"sceNpScoreGetRankingByAccountIdPcIdForCrossSave",signed_out},
    {"sceNpScoreGetFriendsRankingA",signed_out}, {"sceNpScoreGetFriendsRankingForCrossSave",signed_out},
    {"sceNpScoreGetGameDataByAccountId",signed_out},
    {"sceNpMatching2CreateContextA",matching2_context_a},
    {"sceNpMatching2CreateJoinRoomA",matching2_no_context}, {"sceNpMatching2JoinRoomA",matching2_no_context},
    {"sceNpMatching2SendRoomMessage",matching2_no_context}, {"sceNpMatching2SendRoomChatMessage",matching2_no_context},
    {"sceNpMatching2GetRoomDataExternalList",matching2_no_context}, {"sceNpMatching2GetRoomDataInternal",matching2_no_context},
    {"sceNpMatching2GetRoomMemberDataInternal",matching2_no_context},
    {"sceNpMatching2GetRoomMemberIdListLocal",matching2_no_context},
    {"sceNpMatching2GetRoomPasswordLocal",matching2_no_context},
    {"sceNpMatching2GetSignalingOptParamLocal",matching2_no_context},
    {"sceNpMatching2SetSignalingOptParam",matching2_no_context},
    {"sceNpMatching2GetUserInfoListA",matching2_no_context}, {"sceNpMatching2SetUserInfo",matching2_no_context},
    {"sceNpMatching2SignalingGetConnectionInfoA",matching2_no_context},
    {"sceNpMatching2RegisterRoomMessageCallback",matching2_no_context},
    {"sceNpMatching2GetMemoryInfo",ok}, {"sceNpMatching2GetSslMemoryInfo",ok},
    {"sceNpWebApiCreateContextA",webapi_context_a}, {"sceNpWebApiCreateHandle",webapi_handle},
    {"sceNpWebApiDeleteHandle",ok}, {"sceNpWebApiSendRequest2",signed_out},
    {"sceNpWebApiAddHttpRequestHeader",webapi_header}, {"sceNpWebApiGetMemoryPoolStats",ok},
    {"sceNpWebApiCreateExtdPushEventFilter",webapi_filter}, {"sceNpWebApiDeleteExtdPushEventFilter",ok},
    {"sceNpWebApiRegisterExtdPushEventCallbackA",webapi_callback_a},
    {"sceNpWebApiUnregisterExtdPushEventCallback",ok},
    {"sceNpWordFilterCreateTitleCtxA",signed_out}, {"sceNpWordFilterDeleteTitleCtx",community_no_id},
    {"sceNpWordFilterCreateRequest",community_no_id}, {"sceNpWordFilterDeleteRequest",community_no_id},
    {"sceNpWordFilterAbortRequest",community_no_id}, {"sceNpWordFilterSanitizeComment",community_no_id},
    {"sceNpBandwidthTestInitStart",signed_out}, {"sceNpBandwidthTestGetStatus",signed_out},
    {"sceNpBandwidthTestShutdown",signed_out}, {"sceNpBandwidthTestAbort",ok},
    {"sceNpInGameMessageInitialize",object}, {"sceNpInGameMessageTerminate",ok},
    {"sceNpInGameMessageCreateHandle",object}, {"sceNpInGameMessageDeleteHandle",ok},
    {"sceNpInGameMessagePrepareA",signed_out}, {"sceNpInGameMessageSendDataA",signed_out},
    {"sceNpInGameMessageGetMemoryPoolStatistics",ok},
    {"sceNpAuthCreateRequest",auth_request}, {"sceNpAuthGetAuthorizationCodeA",auth_code_a},
    {"sceNpAuthGetIdTokenA",auth_token_a}, {"sceNpAuthAbortRequest",ok},
    {"sceNpCommerceShowPsStoreIcon",ok}, {"sceNpCommerceHidePsStoreIcon",ok},
    {"sceSigninDialogInitialize",signin_init}, {"sceSigninDialogOpen",signin_open},
    {"sceSigninDialogUpdateStatus",signin_status}, {"sceSigninDialogGetResult",signin_result},
    {"sceSigninDialogTerminate",signin_term},
    {"sceInvitationDialogInitialize",invitation_init}, {"sceInvitationDialogOpenA",invitation_open},
    {"sceInvitationDialogUpdateStatus",invitation_status}, {"sceInvitationDialogGetResultA",invitation_result},
    {"sceInvitationDialogTerminate",invitation_term},
    {"sceErrorDialogInitialize",error_init}, {"sceErrorDialogOpen",error_open},
    {"sceErrorDialogUpdateStatus",error_status}, {"sceErrorDialogTerminate",error_term},
    {"sceSocialScreenDialogInitialize",social_init}, {"sceSocialScreenDialogOpen",social_open},
    {"sceSocialScreenDialogUpdateStatus",social_status}, {"sceSocialScreenDialogGetResult",social_result},
    {"sceSocialScreenDialogClose",social_close}, {"sceSocialScreenDialogTerminate",social_term},
};
uintptr_t runtime_np_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
