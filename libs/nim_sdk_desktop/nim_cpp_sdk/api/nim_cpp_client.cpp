﻿/** @file nim_cpp_client.cpp
  * @brief 全局管理功能；主要包括SDK初始化/清理、客户端登录/退出等功能
  * @copyright (c) 2015-2016, NetEase Inc. All rights reserved
  * @author towik, Oleg
  * @date 2015/09/21
  */

#include "nim_cpp_client.h"
#include "nim_sdk_util.h"
#include "nim_json_util.h"
#include "nim_cpp_win32_demo_helper.h"
#include "nim_string_util.h"

namespace nim
{
SDKInstance *g_nim_sdk_instance = NULL;
typedef bool(*nim_client_init)(const char *app_data_dir, const char *app_install_dir, const char *json_extension);
typedef void(*nim_client_cleanup)(const char *json_extension);
typedef void(*nim_client_login)(const char *app_token, const char *account, const char *password, const char *json_extension, nim_json_transport_cb_func cb, const void* user_data);
typedef int (*nim_client_get_login_state)(const char *json_extension);
typedef void(*nim_client_relogin)(const char *json_extension);
typedef void(*nim_client_logout)(nim::NIMLogoutType logout_type, const char *json_extension, nim_json_transport_cb_func cb, const void* user_data);
typedef void(*nim_client_kick_other_client)(const char *json_extension);
typedef void(*nim_client_reg_auto_relogin_cb)(const char *json_extension, nim_json_transport_cb_func cb, const void* user_data);
typedef void(*nim_client_reg_kickout_cb)(const char *json_extension, nim_json_transport_cb_func cb, const void* user_data);
typedef void(*nim_client_reg_disconnect_cb)(const char *json_extension, nim_json_transport_cb_func cb, const void* user_data);
typedef void(*nim_client_reg_multispot_login_notify_cb)(const char *json_extension, nim_json_transport_cb_func cb, const void *user_data);
typedef void(*nim_client_reg_kickout_other_client_cb)(const char *json_extension, nim_json_transport_cb_func cb, const void *user_data);
typedef void(*nim_client_reg_sync_multiport_push_config_cb)(const char *json_extension, nim_client_multiport_push_config_cb_func cb, const void *user_data);
typedef void(*nim_client_set_multiport_push_config)(const char *switch_content, const char *json_extension, nim_client_multiport_push_config_cb_func cb, const void *user_data);
typedef void(*nim_client_get_multiport_push_config)(const char *json_extension, nim_client_multiport_push_config_cb_func cb, const void *user_data);

static void CallbackLogin(const char* json_res, const void *callback)
{
	if (callback != nullptr)
	{
		LoginRes res;
		Json::Value values;
		Json::Reader reader;
		if (reader.parse(PCharToString(json_res), values) && values.isObject())
		{
			res.res_code_ = (NIMResCode)values[kNIMResCode].asInt();
			res.login_step_ = (NIMLoginStep)values[kNIMLoginStep].asUInt();
			res.relogin_ = values[kNIMRelogin].asBool();
			res.retrying_ = values[kNIMRetrying].asBool();
			ParseOtherClientsPres(values[kNIMOtherClientsPres], res.other_clients_);
		}
		Client::LoginCallback *cb = (Client::LoginCallback *)callback;
		PostTaskToUIThread(std::bind((*cb), res));
		//(*cb)(res);
	}
}

static void CallbackLogout(const char *json_res, const void *callback)
{
	if (callback != nullptr)
	{
		Client::LogoutCallback *cb = (Client::LogoutCallback *)callback;
		NIMResCode error_code = kNIMResSuccess;
		Json::Reader reader;
		Json::Value values;
		if (reader.parse(PCharToString(json_res), values) && values.isObject())
		{
			error_code = (NIMResCode)values[kNIMLogoutErrorCode].asInt();
		}
		PostTaskToUIThread(std::bind((*cb), error_code));
		//(*cb)(error_code);
		delete cb;
		cb = nullptr;
	}
}

static void CallbackKickout(const char* json_res, const void *callback)
{
	if (callback != nullptr)
	{
		KickoutRes res;
		Json::Reader reader;
		Json::Value values;
		if (reader.parse(PCharToString(json_res), values) && values.isObject())
		{
			res.client_type_ = (NIMClientType)values[kNIMKickoutClientType].asUInt();
			res.kick_reason_ = (NIMKickReason)values[kNIMKickoutReasonCode].asUInt();
		}
		Client::KickoutCallback *cb = (Client::KickoutCallback *)callback;
		PostTaskToUIThread(std::bind((*cb), res));
		//(*cb)(res);
	}
}

static void CallbackDisconnect(const char* json_res, const void* callback)
{
	if (callback != nullptr)
	{
		Client::DisconnectCallback *cb = (Client::DisconnectCallback *)callback;
		PostTaskToUIThread(std::bind((*cb)));
		//(*cb)();
	}
}

static void CallbackMutliSpotLogin(const char* json_res, const void* callback)
{
	if (callback != nullptr)
	{
		MultiSpotLoginRes res;
		Json::Reader reader;
		Json::Value values;
		if (reader.parse(PCharToString(json_res), values) && values.isObject())
		{
			res.notify_type_ = (NIMMultiSpotNotifyType)values[kNIMMultiSpotNotifyType].asUInt();
			ParseOtherClientsPres(values[kNIMOtherClientsPres], res.other_clients_);
		}
		Client::MultiSpotLoginCallback *cb = (Client::MultiSpotLoginCallback *)callback;
		PostTaskToUIThread(std::bind((*cb), res));
		//(*cb)(res);
	}
}

static void CallbackKickother(const char* json_res, const void* callback)
{
	if (callback != nullptr)
	{
		KickOtherRes res;
		Json::Reader reader;
		Json::Value values;
		if (reader.parse(PCharToString(json_res), values) && values.isObject())
		{
			res.res_code_ = (NIMResCode)values[kNIMKickoutOtherResErrorCode].asInt();
			JsonStrArrayToList(values[kNIMKickoutOtherResDeviceIDs], res.device_ids_);
		}
		Client::KickOtherCallback *cb = (Client::KickOtherCallback *)callback;
		PostTaskToUIThread(std::bind((*cb), res));
		//(*cb)(res);
	}
}

bool Client::Init(const std::string& app_key
	, const std::string& app_data_dir
	, const std::string& app_install_dir
	, const SDKConfig &config)
{
#ifdef NIM_SDK_DLL_IMPORT

#if !defined (WIN32)
	static const char *kSdkNimDll = "libnim.so";
//#elif defined (_DEBUG) || defined (DEBUG)
//	static const char *kSdkNimDll = "nim_d.dll";
#else
	static const char *kSdkNimDll = "nim.dll";
#endif
	if (NULL == g_nim_sdk_instance)
	{
		g_nim_sdk_instance = new SDKInstance();
	}
	if (!g_nim_sdk_instance->LoadSdkDll(app_install_dir.c_str(), kSdkNimDll))
		return false;
#endif

	Json::Value config_root;
	//sdk能力参数（必填）
	Json::Value config_values;
	config_values[nim::kNIMDataBaseEncryptKey] = config.database_encrypt_key_;
	config_values[nim::kNIMPreloadAttach] = config.preload_attach_;	
	config_values[nim::kNIMPreloadImageQuality] = config.preload_image_quality_;
	config_values[nim::kNIMPreloadImageResize] = config.preload_image_resize_;
	config_values[nim::kNIMSDKLogLevel] = config.sdk_log_level_;
	config_values[nim::kNIMSyncSessionAck] = config.sync_session_ack_;
	config_values[nim::kNIMLoginRetryMaxTimes] = config.login_max_retry_times_;
	config_root[nim::kNIMGlobalConfig] = config_values;
	config_root[nim::kNIMAppKey] = app_key;

	if (config.use_private_server_)
	{
		Json::Value srv_config;
		srv_config[nim::kNIMLbsAddress] = config.lbs_address_;
		srv_config[nim::kNIMNosLbsAddress] = config.nos_lbs_address_;
		for (auto iter = config.default_link_address_.begin(); iter != config.default_link_address_.end(); ++iter)
			srv_config[nim::kNIMDefaultLinkAddress].append(*iter);
		for (auto iter = config.default_nos_upload_address_.begin(); iter != config.default_nos_upload_address_.end(); ++iter)
			srv_config[nim::kNIMDefaultNosUploadAddress].append(*iter);
		for (auto iter = config.default_nos_download_address_.begin(); iter != config.default_nos_download_address_.end(); ++iter)
			srv_config[nim::kNIMDefaultNosDownloadAddress].append(*iter);
		for (auto iter = config.default_nos_access_address_.begin(); iter != config.default_nos_access_address_.end(); ++iter)
			srv_config[nim::kNIMDefaultNosAccessAddress].append(*iter);
		srv_config[nim::kNIMRsaPublicKeyModule] = config.rsa_public_key_module_;
		srv_config[nim::kNIMRsaVersion] = config.rsa_version_;
		config_root[nim::kNIMPrivateServerSetting] = srv_config;
	}

	return NIM_SDK_GET_FUNC(nim_client_init)(app_data_dir.c_str(), app_install_dir.c_str(), GetJsonStringWithNoStyled(config_root).c_str());
}

void Client::Cleanup(const std::string& json_extension/* = ""*/)
{
	NIM_SDK_GET_FUNC(nim_client_cleanup)(json_extension.c_str());
#ifdef NIM_SDK_DLL_IMPORT
	g_nim_sdk_instance->UnLoadSdkDll();
	delete g_nim_sdk_instance;
	g_nim_sdk_instance = NULL;
#endif
}

bool Client::Login(const std::string& app_key
	, const std::string& account
	, const std::string& password
	, const LoginCallback& cb
	, const std::string& json_extension/* = ""*/)
{
	if (app_key.empty() || account.empty() || password.empty())
		return false;

	LoginCallback* cb_pointer = nullptr;
	if (cb)
	{
		cb_pointer = new LoginCallback(cb);
	}
	NIM_SDK_GET_FUNC(nim_client_login)(app_key.c_str()
										, account.c_str()
										, password.c_str()
										, json_extension.c_str()
										, &CallbackLogin
										, cb_pointer);

	return true;
}

NIMLoginState Client::GetLoginState( const std::string& json_extension /*= ""*/ )
{
	int login_state = NIM_SDK_GET_FUNC(nim_client_get_login_state)(json_extension.c_str());
	return (NIMLoginState)login_state;
}

void Client::Relogin(const std::string& json_extension/* = ""*/)
{
	return NIM_SDK_GET_FUNC(nim_client_relogin)(json_extension.c_str());
}

void Client::Logout(nim::NIMLogoutType logout_type
	, const LogoutCallback& cb
	, const std::string& json_extension/* = ""*/)
{
	LogoutCallback* cb_pointer = nullptr;
	if (cb)
	{
		cb_pointer = new LogoutCallback(cb);
	}
	return NIM_SDK_GET_FUNC(nim_client_logout)(logout_type, json_extension.c_str(), &CallbackLogout, cb_pointer);
}

bool Client::KickOtherClient(const std::list<std::string>& client_ids)
{
	if (client_ids.empty())
		return false;

	Json::Value values;
	for (auto it = client_ids.begin(); it != client_ids.end(); it++)
	{
		values[nim::kNIMKickoutOtherDeviceIDs].append(*it);
	}
	Json::FastWriter fs;
	std::string out = fs.write(values);
	NIM_SDK_GET_FUNC(nim_client_kick_other_client)(out.c_str());

	return true;
}

static Client::LoginCallback *g_cb_relogin_ = nullptr;
void Client::RegReloginCb(const LoginCallback& cb, const std::string& json_extension/* = ""*/)
{
	if (g_cb_relogin_ != nullptr)
	{
		delete g_cb_relogin_;
		g_cb_relogin_ = nullptr;
	}
	g_cb_relogin_ = new LoginCallback(cb);
	return NIM_SDK_GET_FUNC(nim_client_reg_auto_relogin_cb)(json_extension.c_str(), &CallbackLogin, g_cb_relogin_);
}

static Client::KickoutCallback *g_cb_kickout_ = nullptr;
void Client::RegKickoutCb(const KickoutCallback& cb, const std::string& json_extension/* = ""*/)
{
	if (g_cb_kickout_ != nullptr)
	{
		delete g_cb_kickout_;
		g_cb_kickout_ = nullptr;
	}
	g_cb_kickout_ = new KickoutCallback(cb);
	return NIM_SDK_GET_FUNC(nim_client_reg_kickout_cb)(json_extension.c_str(), &CallbackKickout, g_cb_kickout_);
}

static Client::DisconnectCallback *g_cb_disconnect_ = nullptr;
void Client::RegDisconnectCb(const DisconnectCallback& cb, const std::string& json_extension/* = ""*/)
{
	if (g_cb_disconnect_ != nullptr)
	{
		delete g_cb_disconnect_;
		g_cb_disconnect_ = nullptr;
	}
	g_cb_disconnect_ = new DisconnectCallback(cb);
	return NIM_SDK_GET_FUNC(nim_client_reg_disconnect_cb)(json_extension.c_str(), &CallbackDisconnect, g_cb_disconnect_);
}

static Client::MultiSpotLoginCallback *g_cb_multispot_login_ = nullptr;
void Client::RegMultispotLoginCb(const MultiSpotLoginCallback& cb, const std::string& json_extension/* = ""*/)
{
	if (g_cb_multispot_login_ != nullptr)
	{
		delete g_cb_multispot_login_;
		g_cb_multispot_login_ = nullptr;
	}
	g_cb_multispot_login_ = new MultiSpotLoginCallback(cb);

	return NIM_SDK_GET_FUNC(nim_client_reg_multispot_login_notify_cb)(json_extension.c_str(), &CallbackMutliSpotLogin, g_cb_multispot_login_);
}

static Client::KickOtherCallback *g_cb_kickother_ = nullptr;
void Client::RegKickOtherClientCb(const KickOtherCallback& cb, const std::string& json_extension/* = ""*/)
{
	if (g_cb_kickother_ != nullptr)
	{
		delete g_cb_kickother_;
		g_cb_kickother_ = nullptr;
	}
	g_cb_kickother_ = new KickOtherCallback(cb);

	return NIM_SDK_GET_FUNC(nim_client_reg_kickout_other_client_cb)(json_extension.c_str(), &CallbackKickother, g_cb_kickother_);
}

static void CallbackSyncMultiportPushConfig(int rescode, const char *content, const char *json_extension, const void *user_data)
{
	if (user_data)
	{
		Client::MultiportPushConfigCallback* cb_pointer = (Client::MultiportPushConfigCallback*)user_data;
		if (*cb_pointer)
		{
			Json::Value values;
			Json::Reader reader;
			if (rescode == nim::kNIMResSuccess && reader.parse(PCharToString(content), values) && values.isObject())
			{
				bool open = values[kNIMMultiportPushConfigContentKeyOpen].asInt() == 1;
				PostTaskToUIThread(std::bind((*cb_pointer), rescode, open));
				//(*cb_pointer)(rescode, open);
				return;
			}
			(*cb_pointer)(rescode, false);
		}
	}
}

static Client::MultiportPushConfigCallback* g_cb_sync_multiport_push_switch_ = nullptr;
void Client::RegSyncMultiportPushConfigCb(const MultiportPushConfigCallback& cb, const std::string& json_extension/* = ""*/)
{
	if (g_cb_sync_multiport_push_switch_)
	{
		delete g_cb_sync_multiport_push_switch_;
		g_cb_sync_multiport_push_switch_ = nullptr;
	}
	g_cb_sync_multiport_push_switch_ = new MultiportPushConfigCallback(cb);
	return NIM_SDK_GET_FUNC(nim_client_reg_sync_multiport_push_config_cb)(json_extension.c_str(), &CallbackSyncMultiportPushConfig, g_cb_sync_multiport_push_switch_);
}

static void CallbackMultiportPushConfig(int rescode, const char *content, const char *json_extension, const void *user_data)
{
	if (user_data)
	{
		Client::MultiportPushConfigCallback* cb_pointer = (Client::MultiportPushConfigCallback*)user_data;
		if (*cb_pointer)
		{
			Json::Value values;
			Json::Reader reader;
			if (rescode == nim::kNIMResSuccess && reader.parse(PCharToString(content), values) && values.isObject())
			{
				bool open = values[kNIMMultiportPushConfigContentKeyOpen].asInt() == 1;
				PostTaskToUIThread(std::bind((*cb_pointer), rescode, open));
				//(*cb_pointer)(rescode, open);
				delete cb_pointer;
				return;
			}
			(*cb_pointer)(rescode, false);
		}
		delete cb_pointer;
	}
}

void Client::SetMultiportPushConfigAsync(bool switch_on, const MultiportPushConfigCallback& cb, const std::string& json_extension/* = ""*/)
{
	MultiportPushConfigCallback* cb_pointer = nullptr;
	if (cb)
	{
		cb_pointer = new MultiportPushConfigCallback(cb);
	}
	Json::Value values;
	Json::FastWriter fw;
	values[kNIMMultiportPushConfigContentKeyOpen] = switch_on ? 1 : 2;
	NIM_SDK_GET_FUNC(nim_client_set_multiport_push_config)(GetJsonStringWithNoStyled(values).c_str()
		, json_extension.c_str()
		, &CallbackMultiportPushConfig
		, cb_pointer);
}

void Client::GetMultiportPushConfigAsync(const MultiportPushConfigCallback& cb, const std::string& json_extension/* = ""*/)
{
	MultiportPushConfigCallback* cb_pointer = nullptr;
	if (cb)
	{
		cb_pointer = new MultiportPushConfigCallback(cb);
	}
	NIM_SDK_GET_FUNC(nim_client_get_multiport_push_config)(json_extension.c_str()
		, &CallbackMultiportPushConfig
		, cb_pointer);
}

void Client::UnregClientCb()
{
	if (g_cb_relogin_ != nullptr)
	{
		delete g_cb_relogin_;
		g_cb_relogin_ = nullptr;
	}
	if (g_cb_kickout_ != nullptr)
	{
		delete g_cb_kickout_;
		g_cb_kickout_ = nullptr;
	}
	if (g_cb_disconnect_ != nullptr)
	{
		delete g_cb_disconnect_;
		g_cb_disconnect_ = nullptr;
	}
	if (g_cb_multispot_login_ != nullptr)
	{
		delete g_cb_multispot_login_;
		g_cb_multispot_login_ = nullptr;
	}
	if (g_cb_kickother_ != nullptr)
	{
		delete g_cb_kickother_;
		g_cb_kickother_ = nullptr;
	}
}
}
