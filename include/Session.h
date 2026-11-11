#pragma once
//#define BOOST_ASIO_HAS_STD_COROUTINE
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#include <deque>
#include <stdlib.h>
#include "AsyncPostgresqlConnector.h"
#include "guruxdlms/GXDLMSSecureClient.h"
#include "guruxdlms/GXDLMSDisconnectControl.h"
#include "guruxdlms/GXDLMSData.h"
#include "guruxdlms/GXDLMSTranslator.h"
#include <guruxdlms/GXDLMSClock.h>
#include "guruxdlms/GXDLMSAutoConnect.h"
#include "guruxdlms/GXBytebuffer.h"

#include "funcs.h"
#include "Qgdw.h"
#include "AsyncDatabase.h"
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <string>
#include <vector>
#include "CommandRegistry.h"
#include "DlmsState.h"
#include "SecretManager.h"
#include "spdlog/spdlog.h"
#include "dlms_profile.h"
#include "plugin_loader.h"
using boost::asio::ip::tcp;
using tcp = boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    /*template <typename Connector>
    Session(tcp::socket socket, std::string remote_ip, unsigned short remote_port, Connector&& connector)
        : socket_(std::move(socket)), remote_ip_(std::move(remote_ip)), remote_port_(remote_port),
          connector_(std::forward<Connector>(connector))
    {
        std::cout << "Session creada: " << remote_ip_ << ":" << remote_port_ << "\n";
        remote_port_ = socket_.local_endpoint().port();
    };
    */
    Session(tcp::socket socket, std::string ip, unsigned short port, AsyncPostgresConnector connector, 
        std::shared_ptr<const ami::security::SecretManager> secret_manager)
        : socket_(std::move(socket)), remote_ip_(std::move(ip)), remote_port_(port),
        connector_(connector),
        secret_manager_(std::move(secret_manager)),
	    m_timer_dcu(socket_.get_executor())
    {

        remote_ip_ = socket_.remote_endpoint().address().to_string();
        remote_port_ = socket_.local_endpoint().port();
        printf("connected\n");

    }

    void Close();

    void start();

    //Para que el server sepa cuál medidor corresponde a esta sesión
    void set_on_device_identified(std::function<void(const std::string&)> cb);
    
    // fire and forget, no espera el resultado, solo ejecuta la consulta
    template <typename... Args>
    void execute_sql_ff(const std::string& sql, Args&&... args) {
        std::vector<std::string> params;
        params.reserve(sizeof...(Args));
        (params.push_back(to_string_param(std::forward<Args>(args))), ...);
        
        auto self = shared_from_this();
        boost::asio::co_spawn(socket_.get_executor(),
            [self, sql=std::move(sql), params=std::move(params)]() -> boost::asio::awaitable<void> {
                try {
					auto db = co_await self->connector_();
                    auto result = co_await db->execute_query(sql, params);
                }
                catch (const std::exception& e) {
                    //std::cerr << "[DB ERROR FF] " << sql << " | " << e.what() << std::endl;
					spdlog::error("[DB ERROR FF] {} | {}", sql, e.what());
                }
                co_return;
            },
            boost::asio::detached
        );
    };
    boost::asio::awaitable<ResultSet>query(const std::string& sql, const std::vector<std::string>& params = {});

    boost::asio::awaitable<std::size_t>exec(const std::string& sql, const std::vector<std::string>& params = {});
private:
    const IPlugin* m_plugin = nullptr;
    std::vector<uint8_t>m_hb_buffer;
    bool m_identified = false;

    void InitVars();
    void InitStart();
	boost::asio::awaitable<void> InitStart_AfterParams_Meters();
    
    std::shared_ptr<const ami::security::SecretManager> secret_manager_;

    boost::asio::awaitable<void> do_read_async();
    void do_write(const std::string& message);
    int GetServerAddress(int hes, int dcu);
    void process_message(const std::string& message);
    std::vector<std::vector<std::string>> ParserReply(CGXReplyData reply);
    std::vector<std::string>m_instantaneousvalues;

	boost::asio::awaitable<void>onRawDataAsync(const std::vector<uint8_t>& raw);
	boost::asio::awaitable<void>onMeterRawData(const std::vector<uint8_t>& raw);
	boost::asio::awaitable<void>onDcuRawData(const std::vector<uint8_t>& raw);
	boost::asio::awaitable<void>onZstarRawData(const std::vector<uint8_t>& raw);
	boost::asio::awaitable<void>InitConnectionZstarAsync();
	boost::asio::awaitable<void>processDlmsStateMachine();
	CGXByteBuffer m_buf_dlms;
	std::vector<uint8_t> m_meterRxBuffer;
	std::vector<uint8_t> m_dcuRxBuffer;
	std::vector<uint8_t> m_zstarRxBuffer;
	DlmsClientProfile m_profile;
	enum class SecurityPhase {
		NONE,          //normal association
		BOOTSTRAP,     //connect clear por read with frame counter
		SECURE         //connect with security keys
	};
	SecurityPhase m_securityPhase = SecurityPhase::NONE;
	//modifica la firmapara saber si se inyecta llaves o no
    void ApplyProfileToParser(bool secure, const std::string& secretOwner);
    boost::asio::awaitable<int> SnrmRequest();
    int AARQRequest(CGXByteBuffer& uaResponse);
    boost::asio::awaitable<int> AARQRequestAsync(CGXByteBuffer uaResponse, bool isWrapper = false);
    int AAREResponse(CGXByteBuffer& uaResponse);
    boost::asio::awaitable<int> AAREResponseMeter(CGXByteBuffer uaResponse);
    boost::asio::awaitable<int> AAREResponseDcu(CGXByteBuffer uaResponse);
    void SendInvocationCounter();
    boost::asio::awaitable<int> SendInvocationCounterMeter();
    boost::asio::awaitable<int> DisconnectAsync();
    int UpdateFrameCounter();
    int ReadDLMSPacket(CGXByteBuffer& data);
    boost::asio::awaitable<int>ReadDLMSPacketAsync(CGXByteBuffer data);
    void SendData(CGXByteBuffer data);
    boost::asio::awaitable<int> SendDataAsync(CGXByteBuffer data);
    boost::asio::awaitable<int> SendDataAsync(std::vector<unsigned char> data);
    int ReadDataBlock(std::vector<CGXByteBuffer>& data);
    boost::asio::awaitable<int>ReadDataBlockAsync(std::vector<CGXByteBuffer> data);
    void InitClientCosem();
    boost::asio::awaitable<void> InitClientCosemAsync();
    void ApplyClientProfile(const DlmsClientProfile& p);
    boost::asio::awaitable<void> WriteLogAsync(const std::string& stringlog, int server);
    boost::asio::awaitable <bool> SaveDataFromProfile_co();
    boost::asio::awaitable<bool> SavedataFromProfileAuto_co();
    boost::asio::awaitable<int> GetParamsFromIP(const std::string& ip, int type);
    boost::asio::awaitable<int> UpdatePerfilAsync(std::string sqlvalues);
    void SaveDataFromProfile_Core();
    void SavedataFromProfileAuto_Core();
    //void handle_database_task();
    void UpdateCommandState(int estado);
    int Disconnect();
    void UpdateDBMeterEstatus(int status);
    void GetIdMeter();
    int SendObjDlms(CGXDLMSObject* pObject, int attributeIndex);
    void SetClientCosem(int clientadd, std::string authkey, std::string cipherblockkey, std::string dedicatekey);
    void SendBuffer(std::vector<unsigned char> data);
    void ProcessComandInit_async();
    void ProcessComand(ami::CommandType opc);
    void start_timer_dcu();
    boost::asio::awaitable<void> InitConnectionMeterDcuINH();
    void SNRMRequestINH(std::vector<CGXByteBuffer>& packets);
    void SendInstantaneousValues();
    void ResponseInstantaneousValues();
    void SendProfileGeneric(std::string fechaini, std::string fechafin, int canal);
    void SendProfileGenericBase(std::string fechaini, std::string fechafin, int canal = 0);
    void DisconnectControl(int opc);
    void DisconnectcontrolStatus();
    void WriteProfileGeneric(std::string captureobjects, int numprofile);
    void WritePeriodProfileGeneric(std::string captureperiod, int numprofile);
    void ReadObis(int objectype, std::string obis, int attindex, int flagrequest);
    void WriteObis(int objectype, std::string obis, int attindex, int flagrequest, std::string value);

    std::vector<CGXByteBuffer> Read(CGXDLMSObject* pObject, int attributeIndex);
    
    tcp::socket socket_;
    std::string remote_ip_;
    int remote_port_;

    AsyncPostgresConnector connector_;
    //std::shared_ptr<AsyncDatabase> conn_;
    boost::asio::steady_timer m_timer_dcu;

    enum { max_length = 1024 };
    char data_[max_length]{};
    int m_fabricante;
    // Variables de negocio
    std::string m_concentrator;
    std::string m_metercode;
    int m_typeclcosem{};
    long m_frameCounter = 0;
	bool m_frameCounterValid = false;
    int m_typedevice = 0, m_typesocket, m_updateframecounter = 0, m_inprogress = 0, m_portmacrostar;
    int /*m_dlmsstate = 0,*/ m_conhes = 0x00, m_autoclientcosem = 0, m_typeinterface = 0, m_metertype = 0;
    ami::DlmsState m_dlms_state = ami::DlmsState::Idle;
    int m_modereadprofile = 0, m_numperfil, m_statusprofile, m_initialization=0;
    int m_indexiv = 0;//indice del objeto para valores instantaneos
    int m_relecturas;
    int m_rowid = 0, m_typemodeprofile=0;
    //int m_typecommand;
    struct tm m_start,m_end;
    int m_indexprofile, m_countprofile;
    CGXDLMSObject* m_pObject;
    int m_attributeIndex;
    std::time_t tiempo, m_timecommand;
    std::vector<std::vector<std::string>>m_dataprofile;
    CGXDLMSProfileGeneric* m_objprofile;
    CGXReplyData m_reply;
    //CGXDLMSSecureClient* m_Parser; //version antigua
    std::unique_ptr<CGXDLMSSecureClient> m_Parser;
    int m_loop = 0;
    std::map<std::string, int> m_comands;//mapa de comandos que puede ejecutar el server
    long m_idcomando;//codigo de comando que se esta procesando con este se determina si se genero una peticion manual o automatica si es mayor a 0 es usuario
    std::deque<CGXByteBuffer> m_dataBlock;
    size_t m_indexDataBlock = 0;
    Qgdw m_gdw;
    std::vector<std::string> m_OBISobjects;
    std::vector<std::string> m_ScalerObjects;
    std::vector<std::string> m_arrayobjectsobis;//los objetos que tiene el perfil solicitado
    std::string m_passworddevice;
    std::string m_authenticationkey;
    std::string m_cipherkey;
    std::string m_dedicatekey;
    std::string m_fechaini, m_fechafin,m_accion;
    int m_subtipo, m_numrepeticion;
    // Callback notificado al identificar el medidor o concentrador
    std::function<void(const std::string&)> on_device_identified_;
    // Conversión segura de cualquier tipo a string
    template <typename T>
    std::string to_string_param(T&& value) {
        if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
            return std::to_string(value);
        }
        else if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
            return value;
        }
        else if constexpr (std::is_same_v<std::decay_t<T>, const char*>) {
            return std::string(value);
        }
        else {
            std::ostringstream oss;
            oss << value;
            return oss.str();
        }
    }
	/*codigo nuevo para manejar las opciones de comandos por handlers*/
    using CommandHandler = std::function<void(Session&)>;
    // ─────────────────────────────────────────────────────────────────
    //  Estado del dispatch
    // ─────────────────────────────────────────────────────────────────
    std::unordered_map<ami::CommandType, CommandHandler>  m_dispatch;
    std::unordered_map<std::string, std::string>     m_alias;            // opcional

    // ─────────────────────────────────────────────────────────────────
    //  Ciclo de vida
    // ─────────────────────────────────────────────────────────────────
    void RegisterHandlers();
    
    std::string resolveAlias(const std::string& raw);
    ami::CommandType m_typecommand = ami::CommandType::Unknown;

    // ─────────────────────────────────────────────────────────────────
    //  Handlers (uno por case del switch original)
    // ─────────────────────────────────────────────────────────────────
    void handleInstantaneousValues();
    void handleProfileGeneric();
    void handleReconnections();
    void handleProfileChange();
    void handleProfileTimeChange();
    void handleCustomObisRead();
    void handleMeterIdValidator();
    void handleEventProfilesByDate();
    void handleCustomObisWrite();

    // RF (concentrador)
    void handleRfStopReading();
    void handleRfClearMeasurement();
    void handleRfClearTask();
    void handleRfF33();
    void handleRfF38();
    void handleRfF39();
    void handleRfF40();
    void handleRfReadVersion();
    void handleRfReadNetworkParam();
    void handleRfSetNetworkParam();
    void handleRfAddNetPoint();
    void handleRfSetMeasurementPoint();
    void handleRfAddBroadcastPoint();
    void handleRfStartNetworking();
    void handleRfReadNodeLevel();
    void handleRfStopBuild();
};
