#pragma once
//#define BOOST_ASIO_HAS_STD_COROUTINE
// Detectar si C++20 está disponible
#if __cplusplus >= 202002L && defined(__cpp_impl_coroutine)
#define HAS_CPP20_COROUTINES 1
#else
#define HAS_CPP20_COROUTINES 0
#endif

//#include <boost/asio.hpp>
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

#include "funciones.h"
#include "Qgdw.h"
#include "AsyncDatabase.h"

namespace asio = boost::asio;
using tcp = asio::ip::tcp;


//#include "mapsconf.h"
namespace asio = boost::asio;
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
    Session(tcp::socket socket, std::string ip, unsigned short port, /*DbConnector*/ AsyncPostgresConnector connector)
        : socket_(std::move(socket)), remote_ip_(std::move(ip)), remote_port_(port),
        connector_(connector),
        m_timer_dcu(socket_.get_executor())
    {

        remote_ip_ = socket_.remote_endpoint().address().to_string();
        remote_port_ = socket_.local_endpoint().port();
        printf("dispositivo conectado\n");

    }

    void Close();

    void start();

    //Para que el server sepa cuál medidor corresponde a esta sesión
    void set_on_device_identified(std::function<void(const std::string&)> cb);
    
    template <typename... Args>
    asio::awaitable<ResultSet> fetch_sql_coro(const std::string& sql, Args&&... args) {
        //auto params = make_params(std::forward<Args>(args)...);
        std::vector<std::string> params;
        params.reserve(sizeof...(Args));
        (params.push_back(to_string_param(std::forward<Args>(args))), ...);

        try {
            auto result = co_await conn_->execute_query(sql, params);
            co_return result;
        }
        catch (const std::exception& e) {
            std::cerr << "[DB ERROR] " << sql << " | " << e.what() << std::endl;
            co_return ResultSet{}; // Retorna vacío en caso de error
        }
    }

    template <typename... Args>
    asio::awaitable<void> execute_sql_coro(const std::string& sql, Args&&... args) {
        std::vector<std::string> params;
        params.reserve(sizeof...(Args));
        (params.push_back(to_string_param(std::forward<Args>(args))), ...);
        co_await execute_sql_coro(sql, params);
    }

    template <typename... Args>
    ResultSet fetch_sql(const std::string& sql, Args&&... args) {
        std::vector<std::string> params;
        params.reserve(sizeof...(Args));
        (params.push_back(to_string_param(std::forward<Args>(args))), ...);

        auto self = shared_from_this();

        // Convierte la corrutina en std::future usando asio::use_future
        std::future<ResultSet> fut = asio::co_spawn(socket_.get_executor(), [self, sql, params]() -> asio::awaitable<ResultSet> {
            try {
                auto result = co_await self->conn_->execute_query(sql, params);
                co_return result;
            }
            catch (const std::exception& e) {
                std::cerr << "[DB ERROR] " << sql << " | " << e.what() << std::endl;
                co_return ResultSet{}; // Retorna vacío si hay error
            }
            },
            asio::use_future
        );

        // Espera al resultado de manera bloqueante y lo retorna
        return fut.get();
    }

    template <typename... Args>
    void execute_sql(const std::string& sql, Args&&... args) {
        // Convertir los argumentos a strings
        std::vector<std::string> params;
        params.reserve(sizeof...(Args));
        (params.push_back(to_string_param(std::forward<Args>(args))), ...);
        auto self = shared_from_this();
        // Lanzar la corrutina sin bloquear
        asio::co_spawn(socket_.get_executor(),
            [self, sql, params]() -> asio::awaitable<void> {
                try {
                    auto result = co_await self->conn_->execute_query(sql, params);
                    // Si quieres imprimir o procesar resultados:
                    // process_result(result);
                }
                catch (const std::exception& e) {
                    std::cerr << "[DB ERROR] " << sql << " | " << e.what() << std::endl;
                }
                co_return;
            },
            asio::detached
        );
    };


private:
    void InitVars();
    void InitStart();
    void InitStart_AfterParams_Meters();

    void do_read();
    asio::awaitable<void> do_read_async();
    void do_write(const std::string& message);
    int GetServerAddress(int hes, int dcu);
    void process_message(const std::string& message);
    std::vector<std::vector<std::string>> ParserReply(CGXReplyData reply);
    std::vector<std::string>m_instantaneousvalues;
    void onRawData(const std::vector<uint8_t>& raw);
    asio::awaitable<void>onRawDataAsync(const std::vector<uint8_t>& raw);
    // Paso 1: construye el SNRM y lo envia
    int SnrmRequest();
    // Paso 2: procesar respuesta UA y construye AARQ y lo envia
    int AARQRequest(CGXByteBuffer& uaResponse);
    int AAREResponse(CGXByteBuffer& uaResponse);
    int UpdateFrameCounter();
    int ReadDLMSPacket(CGXByteBuffer& data);
    asio::awaitable<int>ReadDLMSPacketAsync(CGXByteBuffer data);
    void SendData(CGXByteBuffer data);
    asio::awaitable<int> SendDataAsync(CGXByteBuffer data);
    asio::awaitable<int> SendDataAsync(const std::vector<unsigned char>& data);
    int ReadDataBlock(std::vector<CGXByteBuffer>& data);
    asio::awaitable<int>ReadDataBlockAsync(std::vector<CGXByteBuffer>& data);
    void InitClientCosem();
    void InitClientCosemDynamic(const std::unordered_map<int, MeterConfig>& configs, int type);
    asio::awaitable<void> WriteLogAsync(const std::string& stringlog, int server);
#if HAS_CPP20_COROUTINES
    /// <summary>
    /// guarda los datos obtenidos del profile a la base de datos
    /// </summary>
    boost::asio::awaitable <bool> SaveDataFromProfile_co();
    /// <summary>
    /// guarda los datos obtenidos del profile automatico
    /// legacy,puede ser deprecado
    /// </summary>
    /// <returns></returns>
    boost::asio::awaitable <bool> SavedataFromProfileAuto_co();
    boost::asio::awaitable<int> GetParamsFromIP_coro(const std::string& ip, int type);
    boost::asio::awaitable<int> UpdatePerfilAsync(std::string sqlvalues);
#endif
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
    void SendInvocationCounter();
    void ProcessComandInit_async();
    void ProcessComand(int opc);
    void start_timer_dcu();
    void InitConnectionMeterDcuINH();
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
    std::shared_ptr<AsyncDatabase> conn_;
    boost::asio::steady_timer m_timer_dcu;

    enum { max_length = 1024 };
    char data_[max_length]{};
    int m_fabricante;
    // Variables de negocio
    std::string m_concentrator;
    std::string m_codigomedidor;
    int m_typeclcosem{};
    int m_typedevice = 0, m_typesocket, m_updateframecounter = 0, m_inprogress = 0, m_portmacrostar;
    int m_dlmsstate = 0, m_conhes = 0x00, m_autoclientcosem = 0, m_typeinterface = 0, m_metertype = 0;
    int m_modereadprofile = 0, m_numperfil, m_statusprofile, m_initialization=0;
    int m_indexiv = 0;//indice del objeto para valores instantaneos
    int m_relecturas;
    int m_rowid = 0, m_typemodeprofile=0;
    int m_typecommand;
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
    std::string m_passwordmeter;
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
    //Función interna común que realmente hace el trabajo
    asio::awaitable<void> execute_sql_coro(
        const std::string& sql,
        const std::vector<std::string>& params)
    {
        try {
            auto result = co_await conn_->execute_query(sql, params);
            // Aquí puedes manejar result si quieres
        }
        catch (const std::exception& e) {
            std::cerr << "[DB ERROR] " << sql << " | " << e.what() << std::endl;
        }
        co_return;
    }

};





