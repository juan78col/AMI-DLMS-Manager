#include "Session.h"
using namespace std;
//using namespace boost;
//namespace asio = boost::asio;
//using boost::asio::ip::tcp;

#include <typeinfo>
#include <cxxabi.h>
#include <iostream>
#include "AmiQueries.h"
#include "dlms_profile_service.h"
//#include "Qgdw.h"


/*
Session::Session(boost::asio::ip::tcp::socket socket,
    std::string remote_ip,
    unsigned short remote_port,
    DbConnector connector)
    : socket_(std::move(socket)),
    remote_ip_(remote_ip),
    remote_port_(remote_port),
    connector_(std::move(connector)) {

    int status;
    std::cout << "Tipo de DbConnector: "
        << abi::__cxa_demangle(typeid(connector).name(), 0, 0, &status)
        << std::endl;
}*/

void Session::Close() {
    boost::system::error_code ignored_ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    socket_.close(ignored_ec);
}
void Session::start() {
    std::cout << "Sesión iniciada para " << remote_ip_ << ":" << remote_port_ << std::endl;
    //conn_ = connector_();
    InitStart();
    //do_read(); modo callback
    auto self = shared_from_this();
    boost::asio::co_spawn(socket_.get_executor(),
        [self]() -> boost::asio::awaitable<void> {
            co_await self->do_read_async();
            co_return;
        },
        boost::asio::detached);

}

void Session::set_on_device_identified(std::function<void(const std::string&)> cb)
{
    on_device_identified_ = std::move(cb);
}

void Session::InitStart()
{
    InitVars();
    switch (remote_port_)
    {
	case 4059: //client mode meters
    {
        m_typedevice = 1;
        boost::asio::co_spawn(socket_.get_executor(),
            [self = shared_from_this()]() -> boost::asio::awaitable<void> {
                try
                {
                    int rc = co_await self->GetParamsFromIP(self->remote_ip_, self->m_typedevice);
                    if (rc == 0) {
                        co_await self->InitStart_AfterParams_Meters();
                    }
                    else {
                        std::cerr << "Device not registered: " << self->remote_ip_ << std::endl;
                        //get iddevice via heartbeath or dlms request
                    }
                    co_return;
                }
                catch (const std::exception& ex)
                {
                    std::cerr << "error: " << ex.what() << std::endl;
                    co_return;
                }
            },
            boost::asio::detached
        );
        break;
        
    }
    case 8877: //dcu inhemeter
    {
        m_typedevice = 0;
        m_typesocket = 1;
        m_fabricante = 0;//dcu inhemeter

        boost::asio::co_spawn(socket_.get_executor(),
                [self = shared_from_this()]() -> boost::asio::awaitable<void> {
                    try
                    {
                        int rc = co_await self->GetParamsFromIP(self->remote_ip_, self->m_typedevice);
                        if (rc == 0) {
                            self->m_typeclcosem = 6;
                            self->start_timer_dcu();
                        }
                        else {
                            std::cerr << "Device not registered: " << self->remote_ip_ << std::endl;
                            //get iddevice via heartbeath or dlms request
                        }
                        co_return;
                    }
                    catch (const std::exception& ex)
                    {
                        std::cerr << "error: " << ex.what() << std::endl;
                        co_return;
                    }
                },
                boost::asio::detached
            );
        
        break;
    }
    case 8880: // DCU ZStar con transporte DLMS Wrapper
    {
        m_typedevice = 0;
        m_typesocket = 1;
        m_fabricante = 3;

        boost::asio::co_spawn(socket_.get_executor(),
            [self = shared_from_this()]() -> boost::asio::awaitable<void> {
                try
                {
                    const int rc = co_await self->GetParamsFromIP(self->remote_ip_, self->m_typedevice);
                    if (rc != 0) {
                        spdlog::error("[ZStar] DCU no registrado para IP {}", self->remote_ip_);
                    }
                    else {
                        spdlog::info("[{}] DCU ZStar conectado por Wrapper", self->m_concentrator);
                        self->start_timer_dcu();
                    }
                }
                catch (const std::exception& ex)
                {
                    spdlog::error("[ZStar] Error inicializando sesión: {}", ex.what());
                }
                co_return;
            },
            boost::asio::detached);
        break;
    }
    }
}

boost::asio::awaitable<void> Session::InitStart_AfterParams_Meters()
{
    /*co_await InitClientCosemAsync();
    
    if ((!m_authenticationkey.empty() && !m_cipherkey.empty()) && remote_port_ == m_portmacrostar)
    {
        //InitializeConnection(1);
        m_updateframecounter = 1;
        m_inprogress = 1;//rev***
        std::string sqlvalues = "{" + m_metercode + "," + "InitConnFC,0}";
        execute_sql_ff(AmiQueries::CALL_MONITOR, sqlvalues);
		UpdateFrameCounter();
    }
    else {
        SnrmRequest();

    }*/

    // 1. Cargar perfil
    if (!co_await DlmsProfileService::Instance().ResolveForMeter(m_metercode, m_profile)) {
        spdlog::error("[{}] No se encontró perfil para el medidor", m_metercode);
        co_return;
    }

    // 2. Determinar la fase inicial (Basado en Autenticación, NO en SecurityLevel)
    if (m_profile.authentication >= DLMS_AUTHENTICATION_HIGH)
    {
        m_securityPhase = SecurityPhase::BOOTSTRAP;
        spdlog::info("[{}] Perfil HLS detectado. Iniciando Fase Bootstrap (en claro)...", m_metercode);
    }
    else
    {
        m_securityPhase = SecurityPhase::NONE;
        spdlog::info("[{}] Perfil NONE/LOW detectado. Iniciando asociación directa...", m_metercode);
    }

    // 3. Configurar el parser con los parámetros del perfil.
    ApplyProfileToParser(false, m_metercode);
    if (!m_Parser) {
        spdlog::error("[{}] No se pudo crear el cliente DLMS para el perfil {}", m_metercode, m_profile.id);
        m_initialization = -1;
        UpdateCommandState(-1);
        co_return;
    }

    // 4. Arrancar según la interfaz (HDLC requiere SNRM, Wrapper va directo a AARQ).
    int ret = DLMS_ERROR_CODE_OK;
    if (m_typeinterface == 0) {
        ret = co_await SnrmRequest();
    }
    else if (m_typeinterface == 1) {
        ret = co_await AARQRequestAsync(CGXByteBuffer(), true);
    }
    else {
        spdlog::error("[{}] Interfaz DLMS inválida: {}", m_metercode, m_typeinterface);
        ret = DLMS_ERROR_CODE_FALSE;
    }

    if (ret != DLMS_ERROR_CODE_OK) {
        spdlog::error("[{}] Falló el inicio de la conexión DLMS ({})", m_metercode, ret);
        m_dlms_state = ami::DlmsState::Idle;
        m_initialization = -1;
        UpdateCommandState(-1);
    }
}

boost::asio::awaitable<ResultSet>Session::query(const std::string& sql, const std::vector<std::string>& params)
{
    auto db = co_await connector_();
    co_return co_await db->execute_query(sql, params);
    // db muere aquí → vuelve al pool
}

boost::asio::awaitable<std::size_t>Session::exec(const std::string& sql, const std::vector<std::string>& params)
{
    auto db = co_await connector_();
    co_return co_await db->execute_non_query(sql, params);
}

void Session::InitVars()
{
	RegisterHandlers();
    //loadaliases();
	m_idcomando = 0;

}

boost::asio::awaitable<void> Session::do_read_async()
{
    try {
        while (true) {
            std::size_t length = co_await socket_.async_read_some(boost::asio::buffer(data_, max_length), boost::asio::use_awaitable);
            std::vector<uint8_t> raw(data_, data_ + length);
            co_await onRawDataAsync(raw);
            
        }
    }
    catch (const boost::system::system_error& ec)
    {
        switch (ec.code().value())
        {
        case boost::asio::error::eof:
            printf("Disconnect\n");
            break;
        case boost::asio::error::connection_reset:
            printf("Reset Disconnect\n");
            break;
        case boost::asio::error::operation_aborted:
            printf("operation aborted\n");
            break;
        default:
            std::cerr << "Error de lectura: "<< ec.code().message() << " (" << ec.code().value() << ")\n";
            break;
        }
        Close();
    }
    catch (const std::exception& ex) {
        std::cerr << "Error en lectura: " << ex.what() << std::endl;
        Close();
    }

}

void Session::do_write(const std::string& message) {
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(message),
        [self](std::error_code ec, std::size_t /*length*/) {
            if (ec) {
                std::cerr << "Error al enviar respuesta: " << ec.message() << std::endl;
            }
        });
}

int Session::GetServerAddress(int hes, int dcu)
{
    std::string dcucl = lpad(IntToHex(hes), 2, '0') + lpad(IntToHex(dcu), 2, '0');
    return HexToInt(dcucl);
}

void Session::process_message(const std::string& message) {
    std::cout << "Mensaje recibido de " << remote_ip_ << ": " << message << std::endl;
}

std::vector<std::vector<std::string>> Session::ParserReply(CGXReplyData reply)
{
    std::string dat;
    std::vector<std::vector<std::string>> datarray;
    std::vector<std::string> datrow;
    int error = 0;
    //printf("parsereply 1\n");
    try
    {
        switch (reply.GetValue().vt)
        {
        case DLMS_DATA_TYPE_ARRAY:
        {
            for (size_t i = 0; i < (size_t)reply.GetCount(); i++)
            {
                int column = (int)reply.GetValue().Arr[i].Arr.size();
                //printf("parsereply 2 %d \n", column);
                for (size_t j = 0; j < (size_t)column; j++)
                {
                    switch (reply.GetValue().Arr[i].Arr[j].vt)
                    {
                    case DLMS_DATA_TYPE_DATE:
                    case DLMS_DATA_TYPE_DATETIME:
                    {
                        //printf("parsereply 3 ini \n");
                        dat = reply.GetValue().Arr[i].Arr[j].dateTime.ToString();
                        dat = DateToString("%Y-%m-%d %H:%M:%S", reply.GetValue().Arr[i].Arr[j].dateTime.GetValue());
                        //printf("parsereply 3 end %s\n", dat.c_str());
                        break;
                    }
                    case DLMS_DATA_TYPE_UINT16:
                    case DLMS_DATA_TYPE_UINT32:
                    case DLMS_DATA_TYPE_UINT64:
                    case DLMS_DATA_TYPE_INT64:
                    case DLMS_DATA_TYPE_INT8:
                    {
                        //printf("parsereply 4 ini \n");
                        dat = reply.GetValue().Arr[i].Arr[j].ToString();
                        //printf("parsereply 4 end %s\n", dat.c_str());
                        break;
                    }
                    case DLMS_DATA_TYPE_OCTET_STRING:
                    {
                        CGXByteBuffer vg;
                        //printf("parsereply 5 ini \n");
                        reply.GetValue().Arr[i].Arr[j].GetBytes(vg);
                        //printf("parsereply 5 buffer %s\n", GXHelpers::BytesToHex(vg.GetData(), (int)vg.GetSize(),false).c_str());
                        dat = GetDateTimeFromBuffer(vg);
                        //dat = OctetString2DateTime(vg);
                        //printf("parsereply 5 end %s\n", dat.c_str());
                        if (dat.empty() || dat == "")
                        {
                            //printf("%s Fecha incorrecta\n", m_codigomedidor.c_str());
                            error = -6;
                        }
                        break;
                    }
                    default:
                        printf("parsereply Desconocido %d\n", (int)reply.GetValue().Arr[i].Arr[j].vt);
                        break;
                    }
                    datrow.push_back(dat);
                }
                datarray.push_back(datrow);
                datrow.clear();
            }
            break;
        }
        default:
            break;
        }
        //printf("parse reply 2 end\n");
        if (error < 0)//borra el array porque encontro una fecha nula
        {
            datarray.clear();
        }
    }
    catch (const std::exception& e)
    {
        printf("Exception : %s\n", e.what());
        datarray.clear();

    }
    return datarray;
}

boost::asio::awaitable<void> Session::onRawDataAsync(const std::vector<uint8_t>& raw)
{
    CGXByteBuffer buf;
    std::string respuesta = ByteToString((unsigned char*)raw.data(), 0, (int)raw.size());// = GXHelpers::BytesToHex(buf.GetData(), (int)buf.GetSize(), false);
    std::string fecha;
    std::vector<CGXByteBuffer> data;//variable sirve para dcu y meter
    CGXByteBuffer bb, bb2, databuffer;
    CGXReplyData notify;
    std::string manufacturer_id,sqlvalues;

    if (!m_identified)
    {
		bool is_dlms = false;
		if (m_typeinterface == 0 && raw.size() > 0 && raw[0] == 0x7E) is_dlms = true;
		if (m_typeinterface == 1 && raw.size() > 2 && raw[0] == 0x00 && raw[1] == 0x01) is_dlms = true;
		
        if (!is_dlms)
        {
            m_hb_buffer.insert(m_hb_buffer.end(), raw.begin(), raw.end());
            auto& loader = PluginLoader::instance();
            if (!m_plugin)
            {
                m_plugin = loader.match(m_hb_buffer, m_typedevice);
                if (!m_plugin)
                {
                    if (m_hb_buffer.size() > loader.max_magic())
                    {
                        std::cerr << "magic bytes unknown for device type: " << m_typedevice << std::endl;
                        m_hb_buffer.clear();
                        Close();
                    }
                    co_return;
                }
            }
            auto hb = m_plugin->parse(m_hb_buffer);
            if (!hb) {
                co_return; // not enough data yet   
            }
            m_identified = true;
			if (hb->device_id.empty())
			{
				if (!hb->ack_payload.empty())
				{
					co_await SendDataAsync(hb->ack_payload);
				}
                //m_need_dlms_ident = true;
                m_hb_buffer.clear();
				co_return;
			}
            m_typedevice == 0 ? m_concentrator = hb->device_id : m_metercode = hb->device_id;
            manufacturer_id = hb->manufacturer_id;
            sqlvalues = "{" + std::to_string(m_typedevice) + "," + hb->device_id + "," + std::to_string(remote_port_) + "NEW" + " }";
            co_await exec(AmiQueries::CALL_MONITOR, { sqlvalues });

            sqlvalues = "{" + std::to_string(m_typedevice) + "," + hb->device_id + "," + std::to_string(remote_port_) + "HB" + " }";
            co_await exec(AmiQueries::CALL_MONITOR, { sqlvalues });
            tiempo = time(nullptr);

            //auto ack = m_plugin->ack_payload();
            if (!hb->ack_payload.empty())
            {
                co_await SendDataAsync(hb->ack_payload);
            }
            std::string device = m_typedevice == 0 ? m_concentrator : m_metercode;

            if (on_device_identified_)
            {
                on_device_identified_(device);
            }
            m_hb_buffer.clear();
        }

        co_return;

    }
    else
    {
        if (m_typedevice == 1) {
            co_await onMeterRawData(raw);
            co_return;
        }
        if (m_typedevice == 0) {
            if (m_fabricante == 3)
                co_await onZstarRawData(raw);
            else
                co_await onDcuRawData(raw);
            co_return;
        }

        bool is_dlms = false;
        if (m_typeinterface == 0 && raw.size() > 0 && raw[0] == 0x7E) is_dlms = true;
        if (m_typeinterface == 1 && raw.size() > 2 && raw[0] == 0x00 && raw[1] == 0x01) is_dlms = true;
        if (!is_dlms)
        {
            std::string device= m_typedevice == 0 ? m_concentrator : m_metercode;
            sqlvalues = "{" + std::to_string(m_typedevice) + "," + device + "," + std::to_string(remote_port_) + "HB" + " }";
            co_await exec(AmiQueries::CALL_MONITOR, { sqlvalues });
            tiempo = time(nullptr);
            co_return;
        }
    }
	
	if (m_typedevice == 0)
	{
		co_await onDcuRawData(raw);
	}

	/*
    switch (m_typedevice)
    {
    case 0: //dcu inhemeter
    {
        CGXByteBuffer bb, bb2, databuffer;
        CGXReplyData notify;
        Now2(fecha);
        respuesta = "RX:" + fecha + " " + m_concentrator + "|" + m_metercode + "[" + std::to_string((int)m_dlms_state) + "] " + respuesta;
        printf("%s\n", respuesta.c_str());
        
        WriteLog(respuesta + "\n", 3);
        if (raw[0] == 0x0D)
        {
            co_return;
        }
        //Valida si el frame que llega es dlms
        std::vector<unsigned char> buffvector = m_gdw.DecodeFrame(raw.data(), (int)raw.size());
        printf("RX1:%s\n", ByteToString(buffvector.data(), 0, (int)buffvector.size()).c_str());
        buf.Set(buffvector.data(), buffvector.size());
        int lendlms = (int)buffvector.size();
        if (lendlms == 0)
        {
            if (m_gdw.GetSubframeType() == 0x01 && (m_idcomando > 0))
            {
                UpdateCommandState(-9);//offline
                m_idcomando = 0;
                m_inprogress = 0;
                m_numperfil = 0;
                co_return;
            }
            else if (m_gdw.GetSubframeType() == 0x00 && (m_idcomando > 0))
            {
                UpdateCommandState(-1);
                m_idcomando = 0;
                m_inprogress = 0;
                m_numperfil = 0;
                co_return;
            }
            else
            {
                printf("%s no detecto trama\n", m_concentrator.c_str());
                co_return;
            }
        }
        m_typeclcosem = 6;//hdlc para todos
        int opc = m_gdw.Getframetype();
        if (opc == 1) //HB
        {
            if (on_device_identified_)
            {
                on_device_identified_(m_concentrator);
            }
            start_timer_dcu();

            fecha.clear();
            Now2(fecha);
            respuesta = "TX:" + fecha + " " + m_concentrator + "|" + m_metercode + "[" + std::to_string((int)m_dlms_state) + "] " + ByteToString(buffvector.data(), 0, (int)buffvector.size()).c_str();
            printf("%s\n", respuesta.c_str());
            WriteLog(respuesta + "\n", 3);
            co_await SendDataAsync(buffvector);
            co_return;
        }
        if (opc == 3)//rf events
        {
            if (m_inprogress == 0)
            {
                tiempo = time(nullptr);
            }
            std::string rawevent = m_gdw.ParseEvents(buffvector);
            std::string sqlvalues = "{" + m_concentrator + "," + rawevent + "}";
            co_await exec(AmiQueries::SAVEDCUEVENTINH, { sqlvalues });
            co_return;
        }
        
        if (m_loop == 0)
        {
            m_reply.Clear();
        }
        if (m_Parser->GetData(buf, m_reply, notify) == DLMS_ERROR_CODE_FALSE)
        {
            if (m_subtipo == 0)
            {
                m_reply.Clear();
                m_reply = notify;
                notify.Clear();
            }
            if (notify.GetData().GetSize() != 0)
            {
                if (!notify.IsComplete())
                {
                    co_return;
                }
                if (!notify.IsMoreData())
                {
                    //ParseReplyNotification(notify);
                    notify.Clear();
                    co_return;
                }
                //continue;
            }
            /*if ((ret = ReadData(bb, tmp)) != 0)
            {
                if (ret != DLMS_ERROR_CODE_RECEIVE_FAILED || pos == 3)
                {
                    break;
                }
                ++pos;
                printf("Data send failed. Try to resend %d/3\n", pos);
                if ((ret = SendData(data)) != 0)
                {
                    break;
                }
            }
        }
        CGXByteBuffer tmp2 = m_reply.GetData();
        std::cout << (int)m_dlms_state << "-reply0: " << ByteToString(tmp2.GetData(), 0, (int)tmp2.GetSize()) << std::endl;
        if (m_reply.IsMoreData())
        {
            int ret;
            bb2.Clear();

            if ((ret = m_Parser->ReceiverReady(m_reply.GetMoreData(), bb2)) != 0)
            {
                //return ret; retorna error
                printf("Authentication failed 2 (%d) %s\n", ret, CGXDLMSConverter::GetErrorMessage(ret));

            }
            m_loop = 1;
            if ((ret = co_await ReadDLMSPacketAsync(bb2)) != DLMS_ERROR_CODE_OK)
            {
                //return ret; retorna error
            }

        }
        else
        {
            m_loop = 0;
        }
        break;
    }

    case 1://inhemeter
    {
        buf.Set(raw.data(), raw.size());
        int port = remote_port_;
		unsigned long lenbuf = buf.GetSize();
		if (manufacturer_id == "inhemeter" && m_metertype == 1 && m_typeclcosem != 5)
		{
			m_metercode = std::to_string(atol(m_metercode.c_str()));//le quita el cero inicial
		}

		if (m_loop == 0)
		{
			m_reply.Clear();
		}       
		if (m_Parser->GetData(buf, m_reply, notify) == DLMS_ERROR_CODE_FALSE)
		{
			if (notify.GetData().GetSize() != 0)
			{
				if (!notify.IsComplete())
				{
					co_return;
				}
				if (!notify.IsMoreData())
				{
					//ParseReplyNotification(notify); pending
					notify.Clear();
					co_return;
				}
				//continue;
			}
			
		}
		CGXByteBuffer tmp2 = m_reply.GetData();
		std::cout << (int)m_dlms_state << "-reply0: " << ByteToString(tmp2.GetData(), 0, (int)tmp2.GetSize()) << std::endl;
		if (m_reply.IsMoreData())
		{
			int ret;
			bb2.Clear();
			if ((ret = m_Parser->ReceiverReady(m_reply.GetMoreData(), bb2)) != 0)
			{
				//return ret; retorna error
				printf("Authentication failed 2 (%d) %s\n", ret, CGXDLMSConverter::GetErrorMessage(ret));
			}
			m_loop = 1;
			if ((ret = co_await ReadDLMSPacketAsync(bb2)) != DLMS_ERROR_CODE_OK)
			{
				//return ret; retorna error
			}
		}
        else
        {
            m_loop = 0;
        }
        break;
    }

    }

    if (m_loop == 1)
    {
        co_return;
    }

    switch (m_dlms_state)
    {
    case ami::DlmsState::SnrmSent:
    {
        co_await AARQRequestAsync(m_reply.GetData());
        break;
    }
    case ami::DlmsState::AarqSent:
    {
        AAREResponse(m_reply.GetData());
        break;
    }
    case ami::DlmsState::AareSent:
    {
        int ret = m_Parser->ParseApplicationAssociationResponse(m_reply.GetData());
        if (ret != 0)
        {
            printf("Authentication failed (%d) %s\n", ret, CGXDLMSConverter::GetErrorMessage(ret));
            std::string sqlvalues = "{" + m_metercode + "," + "Authentication Failed,0}";
            co_await exec(AmiQueries::CALL_MONITOR, { sqlvalues });
            //return ret;
            m_initialization = -1;
        }
        else
        {
            m_initialization = 0;//ok
            m_inprogress = 0;
            //printf("%s : Initialization ok\n", m_codigomedidor.c_str());
            std::string sqlvalues = "{" + m_metercode + "," + "Initialization Ok,0}";
            co_await exec(AmiQueries::CALL_MONITOR, { sqlvalues });
            if (m_typecommand == ami::CommandType::EventProfilesByDate)
            {
                GetIdMeter();
                m_typecommand = ami::CommandType::Unknown;
            }
        }
        break;
    }
    case ami::DlmsState::DisconnectSent://disconnect response
    {
        int cli = GetServerAddress(m_conhes, 0x01);
        SetClientCosem(cli, m_authenticationkey, m_cipherkey, m_dedicatekey);
        if (co_await SnrmRequest() != DLMS_ERROR_CODE_OK) {
            m_initialization = -1;
            m_dlms_state = ami::DlmsState::Idle;
            UpdateCommandState(-1);
        }
        break;
    }
    case ami::DlmsState::InstantaneousResp://instantaneous values response
    {
        ResponseInstantaneousValues();
        break;
    }
    case ami::DlmsState::ProfileGeneric1Resp://profilegeneric 1 response
    {
        std::string m_valuefromobject;
        switch ((int)m_reply.GetValue().vt)
        {
        case DLMS_DATA_TYPE_ARRAY:
        {
            std::string typeobjects = "[";
            std::string cobis;
            for (size_t i = 0; i < (size_t)m_reply.GetCount(); i++)
            {
                if (i == 0)
                {
                    CGXByteBuffer buf;
                    m_reply.GetValue().Arr[i].Arr[1].GetBytes(buf);
                    GXHelpers::GetLogicalName(buf, cobis);
                    typeobjects += cobis;
                }
                else
                {
                    CGXByteBuffer buf;
                    m_reply.GetValue().Arr[i].Arr[1].GetBytes(buf);
                    GXHelpers::GetLogicalName(buf, cobis);
                    typeobjects += "," + cobis;
                }
            }
            typeobjects += "]";
            m_valuefromobject = typeobjects;
            break;
        }
        default:
            //printf("objects profile data unknown :%d\n", (int)reply.GetValue().vt);
            m_valuefromobject = "";
        }
        //pObject = m_pObject;
        int attributeIndex = m_attributeIndex;
        int ret = m_Parser->UpdateValue(*m_objprofile, attributeIndex, m_reply.GetValue());
        // **************2 parte*************************************
        std::vector<CGXByteBuffer> data;
        CGXReplyData reply;
        //0 - [0.0.1.0.0.255]; 0.001
        //validacion de los objetos del load profile
        if (m_valuefromobject.empty())
        {
            UpdateCommandState(-4);//envio areay de objetos obis vacio o una exepcion para medidores star
            m_dlms_state = ami::DlmsState::Idle;
            m_idcomando = 0;
            m_inprogress = 0;
            m_numperfil = 0;
            //*** Resetear la conexion para medidores star
            if (m_typesocket == 0)
            {
                UpdateDBMeterEstatus(0);
            }
            //SetCloseAndDelete(); anteriomente cerraba socket aqui se podria?

            co_return;
        }
        //printf("profile generic 1 \n");
        if (m_valuefromobject.substr(0, 1) == "[")
        {
            std::string objectsprofile;
            objectsprofile = m_valuefromobject.substr(1, m_valuefromobject.size() - 2);
            std::vector<std::string> arrayobjects;
            Split(objectsprofile, ',', &arrayobjects);
            m_arrayobjectsobis.clear();
            m_arrayobjectsobis = arrayobjects;
            //printf("profile generic 1-1 \n");
            if (arrayobjects.size() < 2)
            {
                //printf("%s perfil 1 FAIL no tiene perfil cargado \n ", m_codigomedidor.c_str());
                UpdateCommandState(-5);
                m_dlms_state = ami::DlmsState::Idle;
                m_idcomando = 0;
                m_inprogress = 0;
                //reseteo variables
                m_numperfil = 0;
                co_return;
            }


            //dectecta si se agrega el obis de rowid para sacarlo de esrructura,medidores microstar
            //printf("profile generic 1-2 \n");
            std::vector<std::string> arrayobjectscompare = arrayobjects;
            if (arrayobjects[0] == "0.0.240.1.1.255")
            {
                m_rowid = 1;
                arrayobjectscompare.erase(arrayobjectscompare.begin());
                m_arrayobjectsobis.erase(m_arrayobjectsobis.begin());
            }
            else
            {
                m_rowid = 0;
            }
            arrayobjectscompare.erase(arrayobjectscompare.begin());
            if ((m_OBISobjects != arrayobjectscompare) && m_numperfil == 0)//verifica si las estrucutras son iguales para primer perfil
            {
                printf("%s perfil Diferente \n ", m_metercode.c_str());
                UpdateCommandState(-2);
                std::string comando = std::to_string(m_idcomando);
                m_dlms_state = ami::DlmsState::Idle;
                m_idcomando = 0;
                m_inprogress = 0;
                m_numperfil = 0;
                //agregado cambio de perfil en linea si existe en sistema
                std::string obiscompare = Implode(",", arrayobjectscompare);

                std::string sqlvalues = "{" + std::to_string(m_metertype) + "," + comando + "," + m_metercode + "," + "\"" + obiscompare + "\"" + "}";
                printf("modificando perfil %s\n", sqlvalues.c_str());

                auto res = co_await query(AmiQueries::UPDATEPROFILE, { sqlvalues });
                if (res.countRows() > 0) {
                    std::string dato = res.get(0, 0);
                    Split(dato, ',', &m_ScalerObjects);
                    m_OBISobjects = arrayobjectscompare;
                }
                co_return;
            }
        }
        //Get meter's send and receive buffers size.
        //int ret;
        //printf("profile generic 2 \n");

        if (m_typemodeprofile == 0)
        {
            ret = m_Parser->ReadRowsByEntry(m_objprofile, m_indexprofile, m_countprofile, data);
        }
        else
        {
            CGXDateTime ini(m_start), fin(m_end);
            ret = m_Parser->ReadRowsByRange(m_objprofile, ini, fin, data);
        }
        if (ret != 0)
        {
            printf("error %s\n", CGXDLMSConverter::GetErrorMessage(ret));
            m_numperfil = 0;
        }
        ret = ReadDataBlock(data);
        //flagproc = 4;
        m_dlms_state = ami::DlmsState::ProfileGeneric2Resp;
        UpdateCommandState(3);
        //if ((ret = m_Parser->ReadRowsByRange(pObject, start, end, data)) != 0 ||
        //    (ret = ReadDataBlock(data, reply)) != 0 ||
        //    (ret = m_Parser->UpdateValue(*pObject, 2, reply.GetValue())) != 0)
        //{
        //    return ret;
        //}

        break;

    }
    case ami::DlmsState::ProfileGeneric2Resp://profile generic 2 response
    {
        int ret = m_Parser->UpdateValue(*m_objprofile, 2, m_reply.GetValue());
        if (ret != 0)
        {
            printf(" %s error UpdateValue: %s\n", m_metercode.c_str(), CGXDLMSConverter::GetErrorMessage(ret));
            m_modereadprofile = 1;
            m_dataprofile.clear();
            m_dataprofile = ParserReply(m_reply);
        }
        else
        {
            m_modereadprofile = 0;
        }
        //****** 2 parte*******************
        if (m_idcomando > 0)
        {
            try {
                bool ok = co_await SaveDataFromProfile_co();
                if (ok) {
                    m_dlms_state = ami::DlmsState::Idle;
                    m_idcomando = 0;
                    m_inprogress = 0;
                    m_numperfil = 0;
                }
            }
            catch (const std::exception& ex) {
                std::cerr << "error en SaveDataFromProfile_co " << ex.what() << std::endl;
            }
            //SaveDataFromProfile();
        }
        else //AUTOMATICO
        {
            try {
                bool ok = co_await SavedataFromProfileAuto_co();
                if (ok) {
                    m_dlms_state = ami::DlmsState::Idle;
                    m_idcomando = 0;
                    m_inprogress = 0;
                    m_numperfil = 0;
                }
            }
            catch (const std::exception& ex) {
                std::cerr << "error SavedataFromProfileAuto_co" << ex.what() << std::endl;
            }
            
        }
        break;
    }
    case ami::DlmsState::InvocationCounterResp://invocationcounter response
    {
        m_updateframecounter = 0;
        unsigned long cont = m_Parser->GetCiphering()->GetInvocationCounter();
        if (cont == 0)
        {
            //cont = 1 + reply.GetValue().ToInteger(); //es global
            m_Parser->GetCiphering()->SetInvocationCounter(cont);
        }
        else
        {
            //m_Parser->GetCiphering()->SetInvocationCounter(cont);
        }

        m_dlms_state = ami::DlmsState::Idle;
        m_initialization = -1;
        Disconnect();

        break;
    }
    case ami::DlmsState::GetMeterIdResp:// getmeterid
    {
        break;
    }

    }
    */
}

boost::asio::awaitable<void> Session::onMeterRawData(const std::vector<uint8_t>& raw)
{
    m_meterRxBuffer.insert(m_meterRxBuffer.end(), raw.begin(), raw.end());
    if (m_meterRxBuffer.size() > 65543) {
        spdlog::error("[{}] Buffer de recepción DLMS excedió el tamaño máximo", m_metercode);
        m_meterRxBuffer.clear();
        m_dlms_state = ami::DlmsState::Idle;
        m_initialization = -1;
        UpdateCommandState(-1);
        co_return;
    }

    bool receivedHeartbeat = false;
    while (!m_meterRxBuffer.empty()) {
        if (m_meterRxBuffer.front() == 0x0D) {
            if (m_meterRxBuffer.size() < 13) break;
            m_meterRxBuffer.erase(m_meterRxBuffer.begin(), m_meterRxBuffer.begin() + 13);
            receivedHeartbeat = true;
            continue;
        }

        size_t frameStart = 0;
        size_t frameEnd = 0;
        if (m_typeinterface == 0) {
            while (frameStart < m_meterRxBuffer.size() && m_meterRxBuffer[frameStart] != 0x7E)
                ++frameStart;
            if (frameStart == m_meterRxBuffer.size()) {
                m_meterRxBuffer.clear();
                receivedHeartbeat = true;
                break;
            }
            if (frameStart > 0) {
                m_meterRxBuffer.erase(m_meterRxBuffer.begin(), m_meterRxBuffer.begin() + frameStart);
                receivedHeartbeat = true;
            }
            if (m_meterRxBuffer.size() < 4) break;
            const size_t hdlcLength =
                (static_cast<size_t>(m_meterRxBuffer[1] & 0x07) << 8) | m_meterRxBuffer[2];
            if (hdlcLength < 5) {
                m_meterRxBuffer.erase(m_meterRxBuffer.begin());
                receivedHeartbeat = true;
                continue;
            }
            frameEnd = hdlcLength + 2;
            if (m_meterRxBuffer.size() < frameEnd) break;
            if (m_meterRxBuffer[frameEnd - 1] != 0x7E) {
                spdlog::warn("[{}] Delimitador final HDLC inválido; resincronizando", m_metercode);
                m_meterRxBuffer.erase(m_meterRxBuffer.begin());
                receivedHeartbeat = true;
                continue;
            }
        }
        else if (m_typeinterface == 1) {
            while (frameStart + 1 < m_meterRxBuffer.size() &&
                !(m_meterRxBuffer[frameStart] == 0x00 && m_meterRxBuffer[frameStart + 1] == 0x01))
                ++frameStart;
            if (frameStart + 1 >= m_meterRxBuffer.size()) {
                const bool keepZero = m_meterRxBuffer.back() == 0x00;
                m_meterRxBuffer.clear();
                if (keepZero) m_meterRxBuffer.push_back(0x00);
                receivedHeartbeat = true;
                break;
            }
            if (frameStart > 0) {
                m_meterRxBuffer.erase(m_meterRxBuffer.begin(), m_meterRxBuffer.begin() + frameStart);
                receivedHeartbeat = true;
            }
            if (m_meterRxBuffer.size() < 8) break;
            const size_t payloadLength = (static_cast<size_t>(m_meterRxBuffer[6]) << 8) | m_meterRxBuffer[7];
            if (payloadLength == 0) {
                m_meterRxBuffer.erase(m_meterRxBuffer.begin());
                receivedHeartbeat = true;
                continue;
            }
            frameEnd = 8 + payloadLength;
            if (m_meterRxBuffer.size() < frameEnd) break;
        }
        else {
            spdlog::error("[{}] Interfaz DLMS inválida al recibir: {}", m_metercode, m_typeinterface);
            m_meterRxBuffer.clear();
            m_dlms_state = ami::DlmsState::Idle;
            m_initialization = -1;
            UpdateCommandState(-1);
            co_return;
        }

        std::vector<uint8_t> frame(m_meterRxBuffer.begin(), m_meterRxBuffer.begin() + frameEnd);
        m_meterRxBuffer.erase(m_meterRxBuffer.begin(), m_meterRxBuffer.begin() + frameEnd);
        m_buf_dlms.Set(frame.data(), frame.size());
        co_await processDlmsStateMachine();
    }

    if (receivedHeartbeat) {
        const std::string device = m_metercode;
        const std::string sqlvalues = "{" + std::to_string(m_typedevice) + "," + device + "," +
            std::to_string(remote_port_) + "HB }";
        co_await exec(AmiQueries::CALL_MONITOR, { sqlvalues });
        tiempo = time(nullptr);
    }
}

boost::asio::awaitable<void> Session::onDcuRawData(const std::vector<uint8_t>& raw)
{
    if (raw.empty()) co_return;

    m_dcuRxBuffer.insert(m_dcuRxBuffer.end(), raw.begin(), raw.end());
    if (m_dcuRxBuffer.size() > 65543) {
        spdlog::error("[{}] Buffer de recepción de DCU excedió el máximo", m_concentrator);
        m_dcuRxBuffer.clear();
        if (m_idcomando > 0) UpdateCommandState(-1);
        m_inprogress = 0;
        co_return;
    }

    while (!m_dcuRxBuffer.empty()) {
        if (m_dcuRxBuffer.front() == 0x0D) {
            if (m_dcuRxBuffer.size() < 13) break;
            m_dcuRxBuffer.erase(m_dcuRxBuffer.begin(), m_dcuRxBuffer.begin() + 13);
            tiempo = time(nullptr);
            continue;
        }

        const auto start = std::find(m_dcuRxBuffer.begin(), m_dcuRxBuffer.end(), 0x68);
        if (start == m_dcuRxBuffer.end()) {
            m_dcuRxBuffer.clear();
            break;
        }
        if (start != m_dcuRxBuffer.begin())
            m_dcuRxBuffer.erase(m_dcuRxBuffer.begin(), start);
        if (m_dcuRxBuffer.size() < 6) break;

        const size_t encodedLength =
            (static_cast<size_t>(m_dcuRxBuffer[2]) << 8) | m_dcuRxBuffer[1];
        const size_t payloadLength = encodedLength >> 2;
        if ((encodedLength & 0x03) != 0x02 ||
            m_dcuRxBuffer[3] != m_dcuRxBuffer[1] ||
            m_dcuRxBuffer[4] != m_dcuRxBuffer[2] ||
            m_dcuRxBuffer[5] != 0x68 || payloadLength > 16383) {
            spdlog::warn("[{}] Cabecera DCU inválida; resincronizando", m_concentrator);
            m_dcuRxBuffer.erase(m_dcuRxBuffer.begin());
            continue;
        }

        const size_t frameLength = payloadLength + 8;
        if (m_dcuRxBuffer.size() < frameLength) break;
        if (m_dcuRxBuffer[frameLength - 1] != 0x16) {
            spdlog::warn("[{}] Fin de trama DCU inválido; resincronizando", m_concentrator);
            m_dcuRxBuffer.erase(m_dcuRxBuffer.begin());
            continue;
        }

        std::vector<unsigned char> frame(m_dcuRxBuffer.begin(), m_dcuRxBuffer.begin() + frameLength);
        m_dcuRxBuffer.erase(m_dcuRxBuffer.begin(), m_dcuRxBuffer.begin() + frameLength);

        const std::string rx = "RX:" + ByteToString(frame.data(), 0, static_cast<int>(frame.size()));
        WriteLog(rx + "\n", 3);
        std::vector<unsigned char> innerDlms = m_gdw.DecodeFrame(frame.data(), static_cast<int>(frame.size()));
        const int frameType = m_gdw.Getframetype();

        if (frameType == 1) {
            if (on_device_identified_) on_device_identified_(m_concentrator);
            start_timer_dcu();
            if (!innerDlms.empty()) {
                const int sendResult = co_await SendDataAsync(std::move(innerDlms));
                if (sendResult != 0)
                    spdlog::error("[{}] No se pudo responder heartbeat DCU", m_concentrator);
            }
            continue;
        }

        if (frameType == 3) {
            if (frame.size() < 32) {
                spdlog::warn("[{}] Evento RF demasiado corto para decodificar", m_concentrator);
                continue;
            }
            if (m_inprogress == 0) tiempo = time(nullptr);
            const std::string event = m_gdw.ParseEvents(frame);
            co_await exec(AmiQueries::SAVEDCUEVENTINH, { "{" + m_concentrator + "," + event + "}" });
            continue;
        }

        if (innerDlms.empty()) {
            if (m_gdw.GetSubframeType() == 0x01 && m_idcomando > 0) {
                UpdateCommandState(-9);
                m_inprogress = 0;
            }
            else if (m_idcomando > 0) {
                UpdateCommandState(-1);
                m_inprogress = 0;
            }
            continue;
        }

        const bool isHdlc = m_typeinterface == 0 && innerDlms.front() == 0x7E;
        const bool isWrapper = m_typeinterface == 1 && innerDlms.size() >= 2 &&
            innerDlms[0] == 0x00 && innerDlms[1] == 0x01;
        if (!isHdlc && !isWrapper) {
            spdlog::debug("[{}] Payload DCU no es una respuesta DLMS para la interfaz {}", m_concentrator, m_typeinterface);
            continue;
        }
        if (!m_Parser) {
            spdlog::error("[{}] Se recibió respuesta DLMS DCU sin cliente inicializado", m_concentrator);
            if (m_idcomando > 0) UpdateCommandState(-1);
            m_inprogress = 0;
            continue;
        }

        m_buf_dlms.Set(innerDlms.data(), innerDlms.size());
        co_await processDlmsStateMachine();
    }
}

boost::asio::awaitable<void> Session::InitConnectionZstarAsync()
{
    m_inprogress = 1;
    m_initialization = 0;

    if (!co_await DlmsProfileService::Instance().ResolveForDcu(m_concentrator, m_profile)) {
        spdlog::error("[{}] No se encontró perfil o configuración DLMS del DCU", m_concentrator);
        m_dlms_state = ami::DlmsState::Idle;
        m_initialization = -1;
        m_inprogress = 0;
        UpdateCommandState(-1);
        co_return;
    }

    if (m_profile.interfaceType != DLMS_INTERFACE_TYPE_WRAPPER ||
        m_profile.typeInterface != DLMS_INTERFACE_TYPE_WRAPPER) {
        spdlog::error("[{}] El perfil ZStar debe configurar DLMS Wrapper (interface_type={}, type_interface={})",
            m_concentrator, m_profile.interfaceType, m_profile.typeInterface);
        m_dlms_state = ami::DlmsState::Idle;
        m_initialization = -1;
        m_inprogress = 0;
        UpdateCommandState(-1);
        co_return;
    }

    if (m_profile.authentication == DLMS_AUTHENTICATION_LOW &&
        ((m_profile.useDevicePassword && m_passworddevice.empty()) ||
            (!m_profile.useDevicePassword && m_profile.passwordLiteral.empty()))) {
        spdlog::error("[{}] El perfil ZStar requiere una contraseña LOW no configurada para el DCU",
            m_concentrator);
        m_dlms_state = ami::DlmsState::Idle;
        m_initialization = -1;
        m_inprogress = 0;
        UpdateCommandState(-1);
        co_return;
    }

    const bool requiresSecureBootstrap =
        m_profile.authentication >= DLMS_AUTHENTICATION_HIGH ||
        m_profile.securityLevel != DLMS_SECURITY_NONE;
    m_securityPhase = requiresSecureBootstrap
        ? SecurityPhase::BOOTSTRAP
        : SecurityPhase::NONE;
    m_frameCounterValid = false;
    m_frameCounter = 0;

    try {
        ApplyProfileToParser(false, m_concentrator);
    }
    catch (const std::exception& ex) {
        spdlog::error("[{}] No se pudo aplicar el perfil DLMS ZStar: {}", m_concentrator, ex.what());
        m_dlms_state = ami::DlmsState::Idle;
        m_initialization = -1;
        m_inprogress = 0;
        UpdateCommandState(-1);
        co_return;
    }
    if ((requiresSecureBootstrap &&
            (m_profile.systemTitleCifrado.empty() || m_profile.cipherKeyCifrada.empty() ||
                m_profile.authenticationKeyCifrada.empty()))) {
        spdlog::error("[{}] El perfil seguro ZStar no tiene todas las credenciales cifradas del DCU",
            m_concentrator);
        m_dlms_state = ami::DlmsState::Idle;
        m_initialization = -1;
        m_inprogress = 0;
        UpdateCommandState(-1);
        co_return;
    }

    if (!m_Parser || m_typeinterface != DLMS_INTERFACE_TYPE_WRAPPER) {
        spdlog::error("[{}] No se pudo inicializar un cliente DLMS Wrapper para ZStar", m_concentrator);
        m_dlms_state = ami::DlmsState::Idle;
        m_initialization = -1;
        m_inprogress = 0;
        UpdateCommandState(-1);
        co_return;
    }

    m_timecommand = time(nullptr);
    const int ret = co_await AARQRequestAsync(CGXByteBuffer(), true);
    if (ret != DLMS_ERROR_CODE_OK) {
        spdlog::error("[{}] Falló el envío de AARQ Wrapper ZStar ({})", m_concentrator, ret);
        m_dlms_state = ami::DlmsState::Idle;
        m_initialization = -1;
        m_inprogress = 0;
        UpdateCommandState(-1);
    }
}

boost::asio::awaitable<void> Session::onZstarRawData(const std::vector<uint8_t>& raw)
{
    if (raw.empty()) co_return;

    m_zstarRxBuffer.insert(m_zstarRxBuffer.end(), raw.begin(), raw.end());
    if (m_zstarRxBuffer.size() > 65543) {
        spdlog::error("[{}] Buffer Wrapper ZStar excedió el máximo", m_concentrator);
        m_zstarRxBuffer.clear();
        if (m_idcomando > 0) UpdateCommandState(-1);
        m_inprogress = 0;
        co_return;
    }

    while (!m_zstarRxBuffer.empty()) {
        auto start = m_zstarRxBuffer.begin();
        while (start + 1 < m_zstarRxBuffer.end() &&
            !(*start == 0x00 && *(start + 1) == 0x01))
            ++start;
        if (start + 1 >= m_zstarRxBuffer.end()) {
            const bool keepZero = m_zstarRxBuffer.back() == 0x00;
            m_zstarRxBuffer.clear();
            if (keepZero) m_zstarRxBuffer.push_back(0x00);
            break;
        }
        if (start != m_zstarRxBuffer.begin())
            m_zstarRxBuffer.erase(m_zstarRxBuffer.begin(), start);
        if (m_zstarRxBuffer.size() < 8) break;

        const size_t payloadLength =
            (static_cast<size_t>(m_zstarRxBuffer[6]) << 8) | m_zstarRxBuffer[7];
        const size_t frameLength = 8 + payloadLength;
        if (frameLength > 65543) {
            spdlog::warn("[{}] Longitud Wrapper ZStar inválida", m_concentrator);
            m_zstarRxBuffer.erase(m_zstarRxBuffer.begin());
            continue;
        }
        if (m_zstarRxBuffer.size() < frameLength) break;

        std::vector<uint8_t> frame(m_zstarRxBuffer.begin(), m_zstarRxBuffer.begin() + frameLength);
        m_zstarRxBuffer.erase(m_zstarRxBuffer.begin(), m_zstarRxBuffer.begin() + frameLength);

        const bool legacyPulse = frame.size() == 23 && frame[5] == 0x01;
        if (payloadLength == 0 || legacyPulse) {
            tiempo = time(nullptr);
            const std::string sqlvalues = "{0," + m_concentrator + "," +
                std::to_string(remote_port_) + "HB }";
            co_await exec(AmiQueries::CALL_MONITOR, { sqlvalues });

            const std::vector<uint8_t> ack = legacyPulse
                ? std::vector<uint8_t>{ 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 }
                : std::vector<uint8_t>{ 0x00, 0x01, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00 };
            const int sendResult = co_await SendDataAsync(ack);
            if (sendResult != 0)
                spdlog::error("[{}] No se pudo responder heartbeat ZStar", m_concentrator);
            continue;
        }

        if (!m_Parser) {
            spdlog::debug("[{}] Wrapper ZStar recibido sin diálogo DLMS activo", m_concentrator);
            continue;
        }

        m_buf_dlms.Set(frame.data(), frame.size());
        co_await processDlmsStateMachine();
    }
}

boost::asio::awaitable<void> Session::processDlmsStateMachine()
{
    CGXReplyData notify;

    if (m_loop == 0) m_reply.Clear();

    // Una respuesta incompleta o una notificación no debe avanzar el diálogo actual.
    if (m_Parser->GetData(m_buf_dlms, m_reply, notify) == DLMS_ERROR_CODE_FALSE)
    {
        if (notify.GetData().GetSize() != 0 && !notify.IsComplete())
            spdlog::debug("[{}] Notificación DLMS incompleta", m_metercode);
        co_return;
    }

    // 2. Manejo de bloques (Block Transfer)
    if (m_reply.IsMoreData())
    {
        CGXByteBuffer bb2;
        const int receiverResult = m_Parser->ReceiverReady(m_reply.GetMoreData(), bb2);
        if (receiverResult != DLMS_ERROR_CODE_OK) {
            spdlog::error("[{}] ReceiverReady falló ({})", m_metercode, receiverResult);
            m_loop = 0;
            m_initialization = -1;
            UpdateCommandState(-1);
            co_return;
        }
        m_loop = 1;
        const int sendResult = co_await ReadDLMSPacketAsync(bb2);
        if (sendResult != DLMS_ERROR_CODE_OK) {
            spdlog::error("[{}] Falló el envío de ReceiverReady ({})", m_metercode, sendResult);
            m_loop = 0;
            m_initialization = -1;
            UpdateCommandState(-1);
        }
        co_return;
    }
    m_loop = 0;

    // 3. Máquina de Estados
    switch (m_dlms_state)
    {
    case ami::DlmsState::SnrmSent:
    {
        const int ret = co_await AARQRequestAsync(m_reply.GetData());
        if (ret != DLMS_ERROR_CODE_OK) {
            spdlog::error("[{}] No se pudo enviar AARQ después de UA ({})", m_metercode, ret);
            m_dlms_state = ami::DlmsState::Idle;
            m_initialization = -1;
            UpdateCommandState(-1);
        }
        break;
    }

    case ami::DlmsState::AarqSent:
    {
        const int ret = m_typedevice == 0
            ? co_await AAREResponseDcu(m_reply.GetData())
            : co_await AAREResponseMeter(m_reply.GetData());
        if (ret != DLMS_ERROR_CODE_OK) {
            spdlog::error("[{}] AARE rechazado o inválido ({})",
                m_typedevice == 0 ? m_concentrator : m_metercode, ret);
            m_dlms_state = ami::DlmsState::Idle;
            m_initialization = -1;
            UpdateCommandState(-1);
        }
        break;
    }

    case ami::DlmsState::AareSent:
    {
        // Este estado SOLO se alcanza en el segundo paso del handshake HLS (GMAC)
        int ret = m_Parser->ParseApplicationAssociationResponse(m_reply.GetData());
        if (ret != 0)
        {
            spdlog::error("[{}] Autenticación HLS fallida ({})", m_metercode, CGXDLMSConverter::GetErrorMessage(ret));
            m_initialization = -1;
            m_dlms_state = ami::DlmsState::Idle;
            UpdateCommandState(-1);
        }
        else
        {
            m_initialization = 0;
            m_inprogress = 0;
            m_dlms_state = ami::DlmsState::Idle;
            spdlog::info("[{}] Asociación HLS completada con éxito.", m_metercode);
            UpdateCommandState(5); // Éxito
            ProcessComand(m_typecommand);
        }
        break;
    }

    case ami::DlmsState::InvocationCounterResp:
    {
        // Recibimos el contador de la fase Bootstrap
        if (m_reply.GetValue().vt != DLMS_DATA_TYPE_UINT32) {
            spdlog::error("[{}] Tipo inválido para Invocation Counter: {}", m_metercode,
                static_cast<int>(m_reply.GetValue().vt));
            m_initialization = -1;
            m_dlms_state = ami::DlmsState::Idle;
            UpdateCommandState(-1);
            break;
        }

        m_frameCounter = m_reply.GetValue().ToInteger();
        if (m_frameCounter < 0) {
            spdlog::error("[{}] Invocation Counter negativo: {}", m_metercode, m_frameCounter);
            m_initialization = -1;
            m_dlms_state = ami::DlmsState::Idle;
            UpdateCommandState(-1);
            break;
        }
        m_frameCounterValid = true;
        m_Parser->GetCiphering()->SetInvocationCounter(m_frameCounter);

        spdlog::info("[{}] Frame Counter obtenido: {}. Preparando conexión segura...", m_metercode, m_frameCounter);
        if (co_await DisconnectAsync() != DLMS_ERROR_CODE_OK) {
            m_initialization = -1;
            m_dlms_state = ami::DlmsState::Idle;
            UpdateCommandState(-1);
        }
        break;
    }

    case ami::DlmsState::DisconnectSent:
    {
        if (m_typedevice == 0 && m_fabricante == 3) {
            if (!m_frameCounterValid) {
                spdlog::error("[{}] No se puede iniciar la asociación segura ZStar sin Invocation Counter válido",
                    m_concentrator);
                m_dlms_state = ami::DlmsState::Idle;
                m_initialization = -1;
                UpdateCommandState(-1);
                break;
            }

            m_securityPhase = SecurityPhase::SECURE;
            try {
                ApplyProfileToParser(true, m_concentrator);
            }
            catch (const std::exception& ex) {
                spdlog::error("[{}] No se pudieron aplicar las credenciales seguras del DCU: {}",
                    m_concentrator, ex.what());
                m_dlms_state = ami::DlmsState::Idle;
                m_initialization = -1;
                UpdateCommandState(-1);
                break;
            }

            const int ret = co_await AARQRequestAsync(CGXByteBuffer(), true);
            if (ret != DLMS_ERROR_CODE_OK) {
                spdlog::error("[{}] Falló el reinicio seguro DLMS Wrapper ZStar ({})", m_concentrator, ret);
                m_dlms_state = ami::DlmsState::Idle;
                m_initialization = -1;
                UpdateCommandState(-1);
            }
            break;
        }

        if (m_typedevice == 0) {
            const int clientAddress = GetServerAddress(m_conhes, 0x01);
            SetClientCosem(clientAddress, m_authenticationkey, m_cipherkey, m_dedicatekey);
            const int ret = co_await SnrmRequest();
            if (ret != DLMS_ERROR_CODE_OK) {
                spdlog::error("[{}] No se pudo reiniciar enlace DLMS a través de DCU ({})", m_concentrator, ret);
                m_dlms_state = ami::DlmsState::Idle;
                m_initialization = -1;
                UpdateCommandState(-1);
            }
            break;
        }

        // Reconexión segura del medidor directo (Fase 3).
        spdlog::info("[{}] Aplicando llaves y configurando canal seguro...", m_metercode);

        if (!m_frameCounterValid) {
            spdlog::error("[{}] No se puede iniciar el canal seguro sin Invocation Counter válido", m_metercode);
            m_dlms_state = ami::DlmsState::Idle;
            m_initialization = -1;
            UpdateCommandState(-1);
            break;
        }

        m_securityPhase = SecurityPhase::SECURE;
        try {
            ApplyProfileToParser(true, m_metercode);
        }
        catch (const std::exception& e) {
            spdlog::error("[{}] No se pudo aplicar el perfil seguro: {}", m_metercode, e.what());
            m_dlms_state = ami::DlmsState::Idle;
            m_initialization = -1;
            UpdateCommandState(-1);
            break;
        }

        // Reconectar según la interfaz
        int ret = DLMS_ERROR_CODE_OK;
        if (m_typeinterface == 0) {
            ret = co_await SnrmRequest();
        }
        else {
            ret = co_await AARQRequestAsync(CGXByteBuffer(), true);
        }
        if (ret != DLMS_ERROR_CODE_OK) {
            spdlog::error("[{}] Falló el reinicio DLMS seguro ({})", m_metercode, ret);
            m_dlms_state = ami::DlmsState::Idle;
            m_initialization = -1;
            UpdateCommandState(-1);
        }
        break;
    }

    // ---------------------------------------------------------
    // Estados de Comandos (Lecturas/Escrituras)
    // ---------------------------------------------------------
    case ami::DlmsState::InstantaneousResp:
        ResponseInstantaneousValues();
        break;

    case ami::DlmsState::ProfileGeneric1Resp:
    {
        std::string m_valuefromobject;
        switch ((int)m_reply.GetValue().vt)
        {
        case DLMS_DATA_TYPE_ARRAY:
        {
            std::string typeobjects = "[";
            std::string cobis;
            for (size_t i = 0; i < (size_t)m_reply.GetCount(); i++)
            {
                if (i == 0)
                {
                    CGXByteBuffer buf;
                    m_reply.GetValue().Arr[i].Arr[1].GetBytes(buf);
                    GXHelpers::GetLogicalName(buf, cobis);
                    typeobjects += cobis;
                }
                else
                {
                    CGXByteBuffer buf;
                    m_reply.GetValue().Arr[i].Arr[1].GetBytes(buf);
                    GXHelpers::GetLogicalName(buf, cobis);
                    typeobjects += "," + cobis;
                }
            }
            typeobjects += "]";
            m_valuefromobject = typeobjects;
            break;
        }
        default:
            //printf("objects profile data unknown :%d\n", (int)reply.GetValue().vt);
            m_valuefromobject = "";
        }
        //pObject = m_pObject;
        int attributeIndex = m_attributeIndex;
        int ret = m_Parser->UpdateValue(*m_objprofile, attributeIndex, m_reply.GetValue());
        // **************2 parte*************************************
        std::vector<CGXByteBuffer> data;
        CGXReplyData reply;
        //0 - [0.0.1.0.0.255]; 0.001
        //validacion de los objetos del load profile
        if (m_valuefromobject.empty())
        {
            UpdateCommandState(-4);//envio areay de objetos obis vacio o una exepcion para medidores star
            m_dlms_state = ami::DlmsState::Idle;
            m_idcomando = 0;
            m_inprogress = 0;
            m_numperfil = 0;
            //*** Resetear la conexion para medidores star
            if (m_typesocket == 0)
            {
                UpdateDBMeterEstatus(0);
            }
            co_return;
        }
        //printf("profile generic 1 \n");
        if (m_valuefromobject.substr(0, 1) == "[")
        {
            std::string objectsprofile;
            objectsprofile = m_valuefromobject.substr(1, m_valuefromobject.size() - 2);
            std::vector<std::string> arrayobjects;
            Split(objectsprofile, ',', &arrayobjects);
            m_arrayobjectsobis.clear();
            m_arrayobjectsobis = arrayobjects;
            //printf("profile generic 1-1 \n");
            if (arrayobjects.size() < 2)
            {
                UpdateCommandState(-5);
                m_dlms_state = ami::DlmsState::Idle;
                m_idcomando = 0;
                m_inprogress = 0;
                m_numperfil = 0;
                co_return;
            }


            //dectecta si se agrega el obis de rowid para sacarlo de esrructura,medidores microstar
            //printf("profile generic 1-2 \n");
            std::vector<std::string> arrayobjectscompare = arrayobjects;
            if (arrayobjects[0] == "0.0.240.1.1.255")
            {
                m_rowid = 1;
                arrayobjectscompare.erase(arrayobjectscompare.begin());
                m_arrayobjectsobis.erase(m_arrayobjectsobis.begin());
            }
            else
            {
                m_rowid = 0;
            }
            arrayobjectscompare.erase(arrayobjectscompare.begin());
            if ((m_OBISobjects != arrayobjectscompare) && m_numperfil == 0)//verifica si las estrucutras son iguales para primer perfil
            {
                printf("%s perfil Diferente \n ", m_metercode.c_str());
                UpdateCommandState(-2);
                std::string comando = std::to_string(m_idcomando);
                m_dlms_state = ami::DlmsState::Idle;
                m_idcomando = 0;
                m_inprogress = 0;
                m_numperfil = 0;
                //agregado cambio de perfil en linea si existe en sistema
                std::string obiscompare = Implode(",", arrayobjectscompare);

                std::string sqlvalues = "{" + std::to_string(m_metertype) + "," + comando + "," + m_metercode + "," + "\"" + obiscompare + "\"" + "}";
                printf("modificando perfil %s\n", sqlvalues.c_str());

                auto res = co_await query(AmiQueries::UPDATEPROFILE, { sqlvalues });
                if (res.countRows() > 0) {
                    std::string dato = res.get(0, 0);
                    Split(dato, ',', &m_ScalerObjects);
                    m_OBISobjects = arrayobjectscompare;
                }
                co_return;
            }
        }
        //Get meter's send and receive buffers size.
        //int ret;
        //printf("profile generic 2 \n");

        if (m_typemodeprofile == 0)
        {
            ret = m_Parser->ReadRowsByEntry(m_objprofile, m_indexprofile, m_countprofile, data);
        }
        else
        {
            CGXDateTime ini(m_start), fin(m_end);
            ret = m_Parser->ReadRowsByRange(m_objprofile, ini, fin, data);
        }
        if (ret != 0)
        {
            printf("error %s\n", CGXDLMSConverter::GetErrorMessage(ret));
            m_numperfil = 0;
        }
        ret = ReadDataBlock(data);
        //flagproc = 4;
        m_dlms_state = ami::DlmsState::ProfileGeneric2Resp;
        UpdateCommandState(3);
        //if ((ret = m_Parser->ReadRowsByRange(pObject, start, end, data)) != 0 ||
        //    (ret = ReadDataBlock(data, reply)) != 0 ||
        //    (ret = m_Parser->UpdateValue(*pObject, 2, reply.GetValue())) != 0)
        //{
        //    return ret;
        //}

        break;

    }
    case ami::DlmsState::ProfileGeneric2Resp:
    {
        int ret = m_Parser->UpdateValue(*m_objprofile, 2, m_reply.GetValue());
        if (ret != 0)
        {
            printf(" %s error UpdateValue: %s\n", m_metercode.c_str(), CGXDLMSConverter::GetErrorMessage(ret));
            m_modereadprofile = 1;
            m_dataprofile.clear();
            m_dataprofile = ParserReply(m_reply);
        }
        else
        {
            m_modereadprofile = 0;
        }
        //****** 2 parte*******************
        if (m_idcomando > 0)
        {
            try {
                bool ok = co_await SaveDataFromProfile_co();
                if (ok) {
                    m_dlms_state = ami::DlmsState::Idle;
                    m_idcomando = 0;
                    m_inprogress = 0;
                    m_numperfil = 0;
                }
            }
            catch (const std::exception& ex) {
                std::cerr << "error en SaveDataFromProfile_co " << ex.what() << std::endl;
            }
            //SaveDataFromProfile();
        }
        else //AUTOMATICO
        {
            try {
                bool ok = co_await SavedataFromProfileAuto_co();
                if (ok) {
                    m_dlms_state = ami::DlmsState::Idle;
                    m_idcomando = 0;
                    m_inprogress = 0;
                    m_numperfil = 0;
                }
            }
            catch (const std::exception& ex) {
                std::cerr << "error SavedataFromProfileAuto_co" << ex.what() << std::endl;
            }

        }
        break;
    }
    case ami::DlmsState::GetMeterIdResp:
        // ... (Tu lógica original de GetMeterIdResp) ...
        break;

    default:
        break;
    }
}

void Session::ApplyProfileToParser(bool secure, const std::string& secretOwner)
{
    // La interfaz del parser y la máquina de estados debe provenir del mismo campo.
    m_typeinterface = static_cast<int>(m_profile.interfaceType);
    if (m_profile.typeInterface != m_typeinterface) {
        spdlog::warn("[{}] interface_type={} difiere de type_interface={}; se usará interface_type",
            secretOwner, m_typeinterface, m_profile.typeInterface);
    }
    m_metertype = m_profile.meterType;

    // 2. Calcular la dirección del servidor (usando helper si está configurado)
    const int serverAddr = m_profile.useServerAddrHelper
        ? CGXDLMSClient::GetServerAddress(
            static_cast<int>(m_profile.helperLogical),
            static_cast<int>(m_profile.helperPhysical),
            static_cast<int>(m_profile.helperAddress))
        : static_cast<int>(m_profile.serverAddress);

    // NONE solo se fuerza durante el bootstrap HLS; NONE/LOW conservan su perfil.
    const bool bootstrap = !secure && m_securityPhase == SecurityPhase::BOOTSTRAP;
    const DLMS_AUTHENTICATION authLevel = bootstrap
        ? DLMS_AUTHENTICATION_NONE
        : static_cast<DLMS_AUTHENTICATION>(m_profile.authentication);

    const std::string& pw = m_profile.useDevicePassword ? m_passworddevice : m_profile.passwordLiteral;
    const char* pwPtr = pw.empty() ? nullptr : pw.c_str();

    // 4. Crear una NUEVA instancia del Parser
    m_Parser = std::make_unique<CGXDLMSSecureClient>(
        m_profile.useLogicalName,
        static_cast<int>(m_profile.clientAddress),
        serverAddr,
        authLevel,
        pwPtr,
        static_cast<DLMS_INTERFACE_TYPE>(m_profile.interfaceType));

    if (!m_Parser) {
        spdlog::error("[{}] ApplyProfileToParser: m_Parser es nulo para el perfil id={}", secretOwner, m_profile.id);
        return;
    }

    // 5. Aplicar conformance
    if (m_profile.hasProposed)
        m_Parser->SetProposedConformance(static_cast<DLMS_CONFORMANCE>(m_profile.proposedConformance));
    if (m_profile.hasNegotiated)
        m_Parser->SetNegotiatedConformance(static_cast<DLMS_CONFORMANCE>(m_profile.negotiatedConformance));

    m_Parser->SetServiceClass(static_cast<DLMS_SERVICE_CLASS>(m_profile.serviceClass));

    if (m_profile.maxReceivePduSize > 0)
        m_Parser->SetMaxReceivePDUSize(static_cast<int>(m_profile.maxReceivePduSize));

    if (m_profile.hasDateTimeSkips)
        m_Parser->SetDateTimeSkips(static_cast<DATETIME_SKIPS>(m_profile.dateTimeSkips));

    // 6. Configuración HDLC (si aplica)
    if (m_profile.hasHdlc) {
        auto& h = m_Parser->GetHdlcSettings();
        h.SetWindowSizeTX(static_cast<int>(m_profile.hdlcWindowTx));
        h.SetWindowSizeRX(static_cast<int>(m_profile.hdlcWindowRx));
        h.SetMaxInfoTX(static_cast<int>(m_profile.hdlcMaxInfoTx));
        h.SetMaxInfoRX(static_cast<int>(m_profile.hdlcMaxInfoRx));
    }

    if (m_profile.setAutoIncreaseInvokeId)
        m_Parser->SetAutoIncreaseInvokeID(m_profile.autoIncreaseInvokeId);

    // 7. CONFIGURACIÓN DE SEGURIDAD (FASE SECURE vs BOOTSTRAP)
    if (secure)
    {
        // Fase segura: Inyectar llaves y configurar suite
        m_Parser->GetCiphering()->SetSecurity(static_cast<DLMS_SECURITY>(m_profile.securityLevel));
        m_Parser->GetCiphering()->SetSecuritySuite(static_cast<DLMS_SECURITY_SUITE>(m_profile.securitySuite));

        try {
            // Descifrar cada llave con su propio AAD (Additional Authenticated Data)
            std::string aadBase = "ami-secret-v1|" + secretOwner + "|";

            std::string systemTitlePlano = secret_manager_->decrypt(
                m_profile.systemTitleCifrado, aadBase + "systemTitle");

            std::string cipherKeyPlano = secret_manager_->decrypt(
                m_profile.cipherKeyCifrada, aadBase + "cipherKey");

            std::string authKeyPlano = secret_manager_->decrypt(
                m_profile.authenticationKeyCifrada, aadBase + "authKey");
            if (m_typedevice == 0 && m_fabricante == 3 &&
                (systemTitlePlano.empty() || cipherKeyPlano.empty() || authKeyPlano.empty())) {
                throw std::runtime_error("El perfil seguro requiere system title, cipher key y authentication key");
            }

            // Inyectar en Gurux
            CGXByteBuffer bb;

            // Authentication Key
            if (!authKeyPlano.empty()) {
                bb.SetHexString(authKeyPlano);
                m_Parser->GetCiphering()->SetAuthenticationKey(bb);
            }

            // Block Cipher Key
            if (!cipherKeyPlano.empty()) {
                bb.Clear();
                bb.SetHexString(cipherKeyPlano);
                m_Parser->GetCiphering()->SetBlockCipherKey(bb);
            }

            // Dedicated Key (Opcional)
            if (!m_profile.dedicatedKeyCifrada.empty()) {
                std::string dedicateKeyPlano = secret_manager_->decrypt(
                    m_profile.dedicatedKeyCifrada, aadBase + "dedicateKey");
                bb.Clear();
                bb.SetHexString(dedicateKeyPlano);
                m_Parser->GetCiphering()->SetDedicatedKey(bb);
            }

            // System Title (Usando el descifrado, NO hardcodeado)
            if (!systemTitlePlano.empty()) {
                bb.Clear();
                bb.AddString(systemTitlePlano.c_str());
                m_Parser->GetCiphering()->SetSystemTitle(bb);
            }

            if (m_frameCounterValid)
                m_Parser->GetCiphering()->SetInvocationCounter(m_frameCounter);

            spdlog::info("[{}] ApplyProfileToParser: Llaves descifradas e inyectadas correctamente.", secretOwner);

        }
        catch (const std::exception& e) {
            spdlog::error("[{}] ApplyProfileToParser: Error descifrando llaves: {}", secretOwner, e.what());
            throw; // Abortamos la conexión si no podemos descifrar
        }
    }
    else
    {
		if (m_securityPhase == SecurityPhase::BOOTSTRAP)
		{
            m_Parser->GetCiphering()->SetSecurity(DLMS_SECURITY_NONE);
            m_Parser->SetAuthentication(DLMS_AUTHENTICATION_NONE);
            spdlog::info("[{}] ApplyProfileToParser: Configurado en modo claro (Bootstrap).", secretOwner);
		}
        else
        {
            m_Parser->GetCiphering()->SetSecurity(DLMS_SECURITY_NONE);
        }
    }
}

boost::asio::awaitable<int> Session::SnrmRequest()
{
    if (!m_Parser) co_return DLMS_ERROR_CODE_FALSE;

    const std::string deviceId = m_typedevice == 0 && m_subtipo == 0
        ? m_concentrator : m_metercode;
    const std::string sqlvalues = "{" + deviceId + ",InitializeConnection-SNMR,0}";
    execute_sql_ff(AmiQueries::CALL_MONITOR, sqlvalues);

    std::vector<CGXByteBuffer> data;
    const int ret = m_Parser->SNRMRequest(data);
    if (ret != DLMS_ERROR_CODE_OK || data.empty()) {
        spdlog::error("[{}] Error creando SNRM ({})", m_metercode, ret);
        co_return ret != DLMS_ERROR_CODE_OK ? ret : DLMS_ERROR_CODE_FALSE;
    }

    m_dlms_state = ami::DlmsState::SnrmSent;
    for (auto& frame : data) {
        const int sendResult = co_await ReadDLMSPacketAsync(frame);
        if (sendResult != DLMS_ERROR_CODE_OK) {
            m_dlms_state = ami::DlmsState::Idle;
            co_return sendResult;
        }
    }
    co_return DLMS_ERROR_CODE_OK;
}

int Session::AARQRequest(CGXByteBuffer& uaResponse)
{
    std::vector<CGXByteBuffer> data;
    //CGXReplyData reply;
    //reply.SetData(uaResponse);

    int ret = m_Parser->ParseUAResponse(uaResponse/*m_reply.GetData()*/);
    if (ret != 0) {
        std::cerr << "[" << m_metercode << "] Error parseando UA\n";
        return DLMS_ERROR_CODE_INVALID_RESPONSE;
    }
    ret = m_Parser->AARQRequest(data);
    if (ret != 0) {
        std::cerr << "[" << m_metercode << "] Error creando AARQ\n";
        return DLMS_ERROR_CODE_FALSE;
    }
    ReadDataBlock(data);
  
    m_dlms_state = ami::DlmsState::AarqSent;
    //std::cout << "AARQRequest OK" << std::endl;
    return DLMS_ERROR_CODE_OK;
}

boost::asio::awaitable<int> Session::AARQRequestAsync(CGXByteBuffer uaResponse, bool isWrapper) {
    std::vector<CGXByteBuffer> data;

    if (!isWrapper)
    {
        int ret = m_Parser->ParseUAResponse(uaResponse);
        if (ret != 0) {
            std::cerr << "[" << m_metercode << "] Error parseando UA\n";
            co_return DLMS_ERROR_CODE_INVALID_RESPONSE;
        }
    }

    int ret = m_Parser->AARQRequest(data);
    if (ret != DLMS_ERROR_CODE_OK || data.empty()) {
        std::cerr << "[" << m_metercode << "] Error creando AARQ\n";
        co_return ret != DLMS_ERROR_CODE_OK ? ret : DLMS_ERROR_CODE_FALSE;
    }

    m_dlms_state = ami::DlmsState::AarqSent;

    const int sendResult = co_await ReadDataBlockAsync(std::move(data));
    co_return sendResult;
}

int Session::AAREResponse(CGXByteBuffer& uaResponse)
{
    const int ret = m_Parser->ParseAAREResponse(uaResponse);
    if (ret != DLMS_ERROR_CODE_OK) {
        m_initialization = -1;
        return ret;
    }

    if (m_updateframecounter == 1) {
        m_initialization = 0;
        SendInvocationCounter();
        return DLMS_ERROR_CODE_OK;
    }

    if (m_Parser->GetAuthentication() > DLMS_AUTHENTICATION_LOW) {
        std::vector<CGXByteBuffer> frames;
        const int requestResult = m_Parser->GetApplicationAssociationRequest(frames);
        if (requestResult != DLMS_ERROR_CODE_OK || frames.empty())
            return requestResult != DLMS_ERROR_CODE_OK ? requestResult : DLMS_ERROR_CODE_FALSE;
        m_dlms_state = ami::DlmsState::AareSent;
        for (auto& frame : frames)
            ReadDLMSPacket(frame);
        return DLMS_ERROR_CODE_OK;
    }

    m_initialization = 0;
    m_inprogress = 0;
    m_dlms_state = ami::DlmsState::Idle;
    execute_sql_ff(AmiQueries::CALL_MONITOR,
        "{" + m_metercode + ",Initialization OK,0}");
    if (m_fabricante == 0 || m_fabricante == 3)
        ProcessComand(m_typecommand);
    else if (m_fabricante == 1 && m_typecommand == ami::CommandType::EventProfilesByDate) {
        GetIdMeter();
        m_typecommand = ami::CommandType::Unknown;
    }
    return DLMS_ERROR_CODE_OK;
}

boost::asio::awaitable<int> Session::AAREResponseDcu(CGXByteBuffer uaResponse)
{
    const int ret = m_Parser->ParseAAREResponse(uaResponse);
    if (ret != DLMS_ERROR_CODE_OK) {
        spdlog::error("[{}] AARE de DCU rechazado ({})", m_concentrator,
            CGXDLMSConverter::GetErrorMessage(ret));
        co_return ret;
    }

    if (m_securityPhase == SecurityPhase::BOOTSTRAP) {
        m_initialization = 0;
        co_return co_await SendInvocationCounterMeter();
    }

    if (m_updateframecounter == 1) {
        m_initialization = 0;
        co_return co_await SendInvocationCounterMeter();
    }

    if (m_Parser->GetAuthentication() > DLMS_AUTHENTICATION_LOW) {
        std::vector<CGXByteBuffer> frames;
        const int requestResult = m_Parser->GetApplicationAssociationRequest(frames);
        if (requestResult != DLMS_ERROR_CODE_OK || frames.empty()) {
            spdlog::error("[{}] No se pudo construir HLS para DCU ({})", m_concentrator, requestResult);
            co_return requestResult != DLMS_ERROR_CODE_OK ? requestResult : DLMS_ERROR_CODE_FALSE;
        }

        m_dlms_state = ami::DlmsState::AareSent;
        co_return co_await ReadDataBlockAsync(std::move(frames));
    }

    m_initialization = 0;
    m_inprogress = 0;
    m_dlms_state = ami::DlmsState::Idle;
    const std::string deviceId = m_subtipo == 0 ? m_concentrator : m_metercode;
    co_await exec(AmiQueries::CALL_MONITOR,
        { "{" + deviceId + ",Initialization OK,0}" });
    if (m_fabricante == 0 || m_fabricante == 3)
        ProcessComand(m_typecommand);
    else if (m_fabricante == 1 && m_typecommand == ami::CommandType::EventProfilesByDate) {
        GetIdMeter();
        m_typecommand = ami::CommandType::Unknown;
    }
    co_return DLMS_ERROR_CODE_OK;
}

boost::asio::awaitable<int> Session::AAREResponseMeter(CGXByteBuffer uaResponse)
{
    /*
    std::vector<CGXByteBuffer> data;//variable sirve para dcu y meter
    int ret = m_Parser->ParseAAREResponse(m_reply.GetData());
    if (ret != 0)
    {
        if (ret == DLMS_ERROR_CODE_APPLICATION_CONTEXT_NAME_NOT_SUPPORTED)
        {
            printf("Use Logical Name referencing is wrong. Change it!\n");
            //return ret;
        }
        printf("AARQRequest failed (%d) %s\n", ret, CGXDLMSConverter::GetErrorMessage(ret));
        m_initialization = -1;
        return DLMS_ERROR_CODE_FALSE;
    }
    if (m_updateframecounter == 1)
    {
        m_initialization = 0;
        SendInvocationCounter();
    }
    else
    {
        if (m_Parser->GetAuthentication() > DLMS_AUTHENTICATION_LOW)
        {
            switch (m_fabricante)
            {
            case 0: //dcu inh
            case 1://meter,macro inh
            {
                ret = m_Parser->GetApplicationAssociationRequest(data);
                for (auto& f : data) {
                    ReadDLMSPacket(f);
                }
                m_dlms_state = ami::DlmsState::AareSent;
                break;
            }
            case 2://meter microstar
            {
                break;
            }
            case 3://dcu star
            {
                long xcv = m_Parser->GetCiphering()->GetInvocationCounter();
                ret = m_Parser->GetApplicationAssociationRequest(data);
                //***SETINVOCATIONCOUNTER***
                m_Parser->GetCiphering()->SetInvocationCounter(xcv + 1);
                //m_InvocationCounter++;//falta definir
                ret = ReadDataBlock(data);
                break;
            }
            }
        }
        else
        {
            m_initialization = 0;
            m_dlms_state = ami::DlmsState::Idle;//varaible clave
            m_inprogress = 0;
            std::string sqlvalues = "{" + m_metercode + "," + "Initialization OK,0}";
            execute_sql_ff(AmiQueries::CALL_MONITOR, sqlvalues);
            switch (m_fabricante)
            {
            case 0:
            case 3:
            {
                ProcessComand(m_typecommand);
                break;
            }
            case 1:
            {
                if (m_typecommand== ami::CommandType::EventProfilesByDate)
                {
                    GetIdMeter();
                    m_typecommand = ami::CommandType::Unknown;
                }
                break;
            }
            }
            
        }
    }
    return 0;
    */
    
    std::vector<CGXByteBuffer> data;
    int ret = m_Parser->ParseAAREResponse(uaResponse);
    if (ret != DLMS_ERROR_CODE_OK) {
        spdlog::error("[{}] AARE no aceptado ({})", m_metercode, CGXDLMSConverter::GetErrorMessage(ret));
        m_initialization = -1;
        UpdateCommandState(-1);
        co_return ret;
    }

    if (m_securityPhase == SecurityPhase::BOOTSTRAP) {
        m_initialization = 0;
        ret = co_await SendInvocationCounterMeter();
        if (ret != DLMS_ERROR_CODE_OK) {
            spdlog::error("[{}] No se pudo solicitar el Invocation Counter ({})", m_metercode, ret);
            m_initialization = -1;
            UpdateCommandState(-1);
        }
        co_return ret;
    }

    if (m_Parser->GetAuthentication() > DLMS_AUTHENTICATION_LOW) {
        ret = m_Parser->GetApplicationAssociationRequest(data);
        if (ret != DLMS_ERROR_CODE_OK || data.empty()) {
            spdlog::error("[{}] No se pudo generar el intercambio HLS ({})", m_metercode, ret);
            m_initialization = -1;
            UpdateCommandState(-1);
            co_return ret != DLMS_ERROR_CODE_OK ? ret : DLMS_ERROR_CODE_FALSE;
        }

        m_dlms_state = ami::DlmsState::AareSent;
        ret = co_await ReadDataBlockAsync(std::move(data));
        if (ret != DLMS_ERROR_CODE_OK) {
            m_dlms_state = ami::DlmsState::Idle;
            m_initialization = -1;
            UpdateCommandState(-1);
        }
        co_return ret;
    }

    m_initialization = 0;
    m_inprogress = 0;
    m_dlms_state = ami::DlmsState::Idle;
    UpdateCommandState(5);
    ProcessComand(m_typecommand);
    co_return DLMS_ERROR_CODE_OK;

}

int Session::UpdateFrameCounter()
{
    int ret = 0;
    //Read frame counter if GeneralProtection is used.
    m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_ACTION));
    std::vector<CGXByteBuffer> data;
    CGXReplyData reply;
    int cli = GetServerAddress(m_conhes, 0x10);
    m_Parser->SetClientAddress((unsigned long)cli); //cli,server=>0xzz10,0x0001
    m_Parser->SetAuthentication(DLMS_AUTHENTICATION_NONE);
    m_Parser->GetCiphering()->SetSecurity(DLMS_SECURITY_NONE);
    //Get meter's send and receive buffers size.
    ret = m_Parser->SNRMRequest(data);
    ret = ReadDataBlock(data);
    if (ret == 0 && data.size() == 0)
     {
        ret = m_Parser->ParseUAResponse(reply.GetData());
        if (ret == 0)
          {
             reply.Clear();
             ret = m_Parser->AARQRequest(data);
             ret = ReadDataBlock(data);
             m_dlms_state = ami::DlmsState::AarqSent;
          }
     }
     else
     {
        m_dlms_state = ami::DlmsState::SnrmSent;
     }

     reply.Clear();
    
     return ret;

}

int Session::ReadDLMSPacket(CGXByteBuffer& data)
{
    if (data.GetSize() == 0) return 0;
    if (remote_port_ == 8877)
    {
        std::string dlms = ByteToString(data.GetData(), 0, (int)data.GetSize());
        if (dlms == "" || dlms.empty())
        {
            printf("dlms vacio \n");
        }
        else
        {
            printf("TX1:%s\n", dlms.c_str());
        }
        std::vector<unsigned char> datos = m_gdw.PacketFrame2(dlms, m_concentrator, m_metercode);
        CGXByteBuffer dtemp;
        dtemp.Set(datos.data(), datos.size());
        std::string respuesta, fecha;
        Now2(fecha);
        respuesta = "TX:" + fecha + " " + m_concentrator + "|" + m_metercode + "[" + std::to_string((int)m_dlms_state) + "] " + ByteToString(datos.data(), 0, (int)datos.size()).c_str();
        printf("%s\n", respuesta.c_str());
        WriteLog(respuesta + "\n", 3);
        SendData(dtemp);

    }
    else
    {
        std::cout << "[" << m_metercode << "] TX Frame: " << data.ToHexString() << "\n";
        SendData(data);

    }
    
    return 0;
}

boost::asio::awaitable<int> Session::ReadDLMSPacketAsync(CGXByteBuffer data)
{
    if (data.GetSize() == 0) co_return 0;
    CGXByteBuffer out;
    if (remote_port_ == 8877)
    {
        std::string dlms = ByteToString(data.GetData(), 0, (int)data.GetSize());
        if (dlms.empty()) {
            printf("dlms vacio \n");
        }
        else {
            printf("TX1:%s\n", dlms.c_str());
        }

        std::vector<unsigned char> datos = m_gdw.PacketFrame2(dlms, m_concentrator, m_metercode);
        out.Set(datos.data(), datos.size());

        std::string respuesta, fecha;
        Now2(fecha);
        respuesta = "TX:" + fecha + " " + m_concentrator + "|" + m_metercode + "[" + std::to_string((int)m_dlms_state) + "] " + ByteToString(datos.data(), 0, (int)datos.size()).c_str();
        printf("%s\n", respuesta.c_str());
        WriteLog(respuesta + "\n", 3);
    }
    else
    {
        std::cout << "[" << m_metercode << "] TX Frame: " << data.ToHexString() << "\n";
		out = std::move(data);
    }
	co_return co_await SendDataAsync(std::move(out));
}

void Session::SendData(CGXByteBuffer data)
{
    /*auto self(this->shared_from_this());
    asio::async_write(socket_, asio::buffer(data.GetData(), data.GetSize()),
        [this, self, data = std::move(data)](std::error_code ec, std::size_t) {
            if (!ec) {
                std::cout << "[" << m_codigomedidor << "] \n";// << data.ToHexString() << "\n";

            }
            else {
                std::cout << "Error enviando a " << m_codigomedidor << "\n";
            }
        });
        */
    auto self = shared_from_this();
    boost::asio::co_spawn(socket_.get_executor(),
        [self, data = std::move(data)]() -> boost::asio::awaitable<void> {
            int result = co_await self->SendDataAsync(std::move(data));
            // Puedes agregar lógica adicional según el resultado si lo deseas
            co_return;
        },
        boost::asio::detached);

}

boost::asio::awaitable<int> Session::SendDataAsync(CGXByteBuffer data)
{
    try {
        co_await boost::asio::async_write(socket_, boost::asio::buffer(data.GetData(), data.GetSize()), 
            boost::asio::use_awaitable);
        //std::cout << "[" << m_codigomedidor << "]\n"; // << data.ToHexString() << "\n";
        co_return 0; // éxito
    }
    catch (const std::exception& ex) {
        std::cout << "Error enviando a " << m_metercode << ": " << ex.what() << "\n";
        co_return -1; // error
    }
}

boost::asio::awaitable<int> Session::SendDataAsync(std::vector<unsigned char> data)
{
    try {
        co_await boost::asio::async_write(socket_, boost::asio::buffer(data.data(), data.size()), 
            boost::asio::use_awaitable);
        co_return 0;
    }
    catch (...) {
        co_return -1;
    }

}

int Session::ReadDataBlock(std::vector<CGXByteBuffer>& data)
{
    if (data.empty()) return 0;

    m_dataBlock.assign(data.begin(), data.end());
    m_indexDataBlock = 0;
    // enviar primer bloque
    return ReadDLMSPacket(m_dataBlock[m_indexDataBlock]);
}

boost::asio::awaitable<int> Session::ReadDataBlockAsync(std::vector<CGXByteBuffer> data)
{
    if (data.empty()) co_return 0;

    m_dataBlock.assign(data.begin(), data.end());
    m_indexDataBlock = 0;

    int result = co_await ReadDLMSPacketAsync(m_dataBlock[m_indexDataBlock]);
    co_return result;
}

[[deprecated("This function is deprecated. Use initclientcosemasync instead.")]]
void Session::InitClientCosem()
{
	if (m_autoclientcosem == 1)
    {
        //InitClientCosemDynamic(m_MeterConfigurations, m_typeclcosem); pendiente de  implementar
        return;
    }
    switch (m_typeclcosem)
    {
    case 0://itron macros
    {
        m_typeinterface = 0;
        //serveradrres 145
        //m_Parser = new CGXDLMSSecureClient(true, 1, CGXDLMSClient::GetServerAddress(1, 0x11, 0), DLMS_AUTHENTICATION_LOW, "ABCDEFGH", DLMS_INTERFACE_TYPE_HDLC); //itron
        //m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 1, CGXDLMSClient::GetServerAddress(1, 0x11, 0), DLMS_AUTHENTICATION_LOW, "ABCDEFGH", DLMS_INTERFACE_TYPE_HDLC);
        m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_ACTION));
        m_Parser->SetNegotiatedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_ACTION));
        m_Parser->SetServiceClass(DLMS_SERVICE_CLASS::DLMS_SERVICE_CLASS_CONFIRMED);
        m_metertype = 0;
        break;
    }
    case 1://medidores centralizada reservado
    {
        break;
    }
    case 2://medidores ziv plc reservado
    {
        break;
    }
    case 3://medidores emsitech microstar
    {
        m_typeinterface = 0;
        if (m_passworddevice == "")
        {
            m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 16, 1);

        }
        else
        {
            m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 0x20, 1, DLMS_AUTHENTICATION_LOW, m_passworddevice.c_str());

        }

        m_Parser->SetDateTimeSkips((DATETIME_SKIPS)(DATETIME_SKIPS_DEVITATION | DATETIME_SKIPS_STATUS));
        m_metertype = 1;
        break;
    }
    case 4://inhemeter
    {
        m_typeinterface = 1;
        //m_Parser = new CGXDLMSSecureClient(true, 1, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, "INHE-0410-WRITE", DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 1, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, m_passworddevice.c_str(), DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_GENERAL_PROTECTION | DLMS_CONFORMANCE_GENERAL_BLOCK_TRANSFER | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_SET | DLMS_CONFORMANCE_PRIORITY_MGMT_SUPPORTED | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_GET | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_DATA_NOTIFICATION | DLMS_CONFORMANCE_ACCESS | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION));
        m_Parser->SetServiceClass(DLMS_SERVICE_CLASS::DLMS_SERVICE_CLASS_CONFIRMED);
        m_Parser->SetMaxReceivePDUSize(0x200);
        m_Parser->SetDateTimeSkips((DATETIME_SKIPS)(DATETIME_SKIPS_DEVITATION | DATETIME_SKIPS_STATUS));
        m_metertype = 1;
        break;
    }
    case 5://inhemeter macro como medidor
    {
        m_typeinterface = 1;
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 18, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, m_passworddevice.c_str(), DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_GENERAL_PROTECTION | DLMS_CONFORMANCE_GENERAL_BLOCK_TRANSFER | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_SET | DLMS_CONFORMANCE_PRIORITY_MGMT_SUPPORTED | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_GET | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_DATA_NOTIFICATION | DLMS_CONFORMANCE_ACCESS | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION));
        m_Parser->SetServiceClass(DLMS_SERVICE_CLASS::DLMS_SERVICE_CLASS_CONFIRMED);
        m_Parser->SetMaxReceivePDUSize(0x200);
        m_Parser->SetDateTimeSkips((DATETIME_SKIPS)(DATETIME_SKIPS_DEVITATION | DATETIME_SKIPS_STATUS));
        m_metertype = 1;
        break;
    }
    case 6://inhemeter dcu rf
    {
        m_typeinterface = 0;
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 1, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, m_passworddevice.c_str(), DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_HDLC);
        m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_GENERAL_PROTECTION | DLMS_CONFORMANCE_GENERAL_BLOCK_TRANSFER | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_SET | DLMS_CONFORMANCE_PRIORITY_MGMT_SUPPORTED | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_GET | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_DATA_NOTIFICATION | DLMS_CONFORMANCE_ACCESS | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION));
        m_Parser->SetServiceClass(DLMS_SERVICE_CLASS::DLMS_SERVICE_CLASS_CONFIRMED);
        m_Parser->SetMaxReceivePDUSize(0x200);
        m_Parser->GetHdlcSettings().SetWindowSizeTX(1);
        m_Parser->GetHdlcSettings().SetWindowSizeRX(1);
        m_Parser->GetHdlcSettings().SetMaxInfoTX(230); //200
        m_Parser->GetHdlcSettings().SetMaxInfoRX(230); //200
        m_metertype = 1;
        break;
    }
    case 7://macro star with keys
    {
        m_typeinterface = 1;
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 1, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_NONE, nullptr, DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_ACCESS));
        m_Parser->SetServiceClass(DLMS_SERVICE_CLASS::DLMS_SERVICE_CLASS_CONFIRMED);
        m_Parser->SetAutoIncreaseInvokeID(true);
        m_metertype = 0;
        break;
    }
    case 8://star meters, por verificar
    {
        m_typeinterface = 1;
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 0x10, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_NONE, nullptr, DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser->SetNegotiatedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE::DLMS_CONFORMANCE_GET));
        m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE::DLMS_CONFORMANCE_GET));
        m_Parser->SetServiceClass(DLMS_SERVICE_CLASS::DLMS_SERVICE_CLASS_CONFIRMED);
        m_metertype = 1;
        break;
    }
    case 40://inhemeter macromedicion
    {
        m_typeinterface = 1;
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 18, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, m_passworddevice.c_str(), DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_GENERAL_PROTECTION | DLMS_CONFORMANCE_GENERAL_BLOCK_TRANSFER | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_SET | DLMS_CONFORMANCE_PRIORITY_MGMT_SUPPORTED | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_GET | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_DATA_NOTIFICATION | DLMS_CONFORMANCE_ACCESS | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION));
        m_Parser->SetServiceClass(DLMS_SERVICE_CLASS::DLMS_SERVICE_CLASS_CONFIRMED);
        m_Parser->SetDateTimeSkips((DATETIME_SKIPS)(DATETIME_SKIPS_DEVITATION | DATETIME_SKIPS_STATUS));
        m_Parser->SetMaxReceivePDUSize(0x200);
        
        m_metertype = 0;
        break;
    }
    default:
        break;
    }
    if (m_Parser != NULL)//aplica para itron emsiech por ahora
    {
        m_Parser->GetCiphering()->SetSecurity(DLMS_SECURITY::DLMS_SECURITY_NONE);
        m_Parser->SetAutoIncreaseInvokeID(false);

    }
}

boost::asio::awaitable<void> Session::InitClientCosemAsync()
{
    DlmsClientProfile p;

    if (m_autoclientcosem == 1) {
        if (!co_await DlmsProfileService::Instance().ResolveForMeter(m_metercode, p)) {
            spdlog::warn("InitClientCosem: no profile for meter {}", m_metercode);
            co_return;
        }
    }
    else {
        if (!DlmsProfileService::Instance().GetByTypeclcosem(m_typeclcosem, p)) {
            spdlog::warn("InitClientCosem: no profile for typeclcosem={}", m_typeclcosem);
            co_return;
        }
    }

    ApplyClientProfile(p);
    co_return;
}

void Session::ApplyClientProfile(const DlmsClientProfile& p)
{
    m_typeinterface = p.typeInterface;
    m_metertype = p.meterType;

    const int serverAddr = p.useServerAddrHelper
        ? CGXDLMSClient::GetServerAddress(
            static_cast<int>(p.helperLogical),
            static_cast<int>(p.helperPhysical),
            static_cast<int>(p.helperAddress))
        : static_cast<int>(p.serverAddress);

    const std::string& pw = p.useDevicePassword ? m_passworddevice : p.passwordLiteral;
    const char* pwPtr = pw.empty() ? nullptr : pw.c_str();

    m_Parser = std::make_unique<CGXDLMSSecureClient>(
        p.useLogicalName,
        static_cast<int>(p.clientAddress),
        serverAddr,
        static_cast<DLMS_AUTHENTICATION>(p.authentication),
        pwPtr,
        static_cast<DLMS_INTERFACE_TYPE>(p.interfaceType));

    if (!m_Parser) {
        spdlog::error("ApplyClientProfile: m_Parser is null for profile id={}", p.id);
        return;
    }

    if (p.hasProposed)
        m_Parser->SetProposedConformance(
            static_cast<DLMS_CONFORMANCE>(p.proposedConformance));
    if (p.hasNegotiated)
        m_Parser->SetNegotiatedConformance(
            static_cast<DLMS_CONFORMANCE>(p.negotiatedConformance));

    m_Parser->SetServiceClass(static_cast<DLMS_SERVICE_CLASS>(p.serviceClass));

    if (p.maxReceivePduSize > 0)
        m_Parser->SetMaxReceivePDUSize(static_cast<int>(p.maxReceivePduSize));

    if (p.hasDateTimeSkips)
        m_Parser->SetDateTimeSkips(static_cast<DATETIME_SKIPS>(p.dateTimeSkips));

    if (p.hasHdlc) {
        auto& h = m_Parser->GetHdlcSettings();
        h.SetWindowSizeTX(static_cast<int>(p.hdlcWindowTx));
        h.SetWindowSizeRX(static_cast<int>(p.hdlcWindowRx));
        h.SetMaxInfoTX(static_cast<int>(p.hdlcMaxInfoTx));
        h.SetMaxInfoRX(static_cast<int>(p.hdlcMaxInfoRx));
    }

    if (p.setAutoIncreaseInvokeId)
        m_Parser->SetAutoIncreaseInvokeID(p.autoIncreaseInvokeId);

    m_Parser->GetCiphering()->SetSecurity(
        static_cast<DLMS_SECURITY>(p.security));

    m_Parser->GetCiphering()->SetSecurity(static_cast<DLMS_SECURITY>(p.securityLevel));

    // Si el nivel de seguridad NO es NONE, desciframos las llaves
    if (p.securityLevel != 0) { // 0 = DLMS_SECURITY_NONE

        try {
            // 1. Descifrar las llaves usando el SecretManager
            //std::string aadFor(std::string_view meter, std::string_view column) {
            //    return "ami-secret-v1|" + std::string(meter) + "|" + std::string(column);
            //}
			std::string add = "ami-secret-v1|" + m_metercode + "|systemTitle";//pendiente de realizar es un dummy
            std::string systemTitlePlano = secret_manager_->decrypt(p.systemTitleCifrado,add);
            m_cipherkey = secret_manager_->decrypt(p.cipherKeyCifrada,add);
            m_authenticationkey = secret_manager_->decrypt(p.authenticationKeyCifrada,add);

            // Opcional: Dedicated Key (si aplica)
            std::string dedicatedKeyPlano;
            if (!p.dedicatedKeyCifrada.empty()) {
                m_dedicatekey = secret_manager_->decrypt(p.dedicatedKeyCifrada,add);
            }
            //SetClientCosem(m_concentrator, authKeyPlano, cipherKeyPlano, dedicatedKeyPlano);
            CGXByteBuffer bb;
            bb.SetHexString(m_authenticationkey);
            m_Parser->GetCiphering()->SetAuthenticationKey(bb);
            bb.Clear();
            bb.SetHexString(m_cipherkey);
            m_Parser->GetCiphering()->SetBlockCipherKey(bb);
            bb.Clear();
            bb.SetHexString(m_dedicatekey);
            m_Parser->GetCiphering()->SetDedicatedKey(bb);
            bb.Clear();
            bb.AddString("AMIDLMS0");
            m_Parser->GetCiphering()->SetSystemTitle(bb);
            spdlog::info("ApplyClientProfile: Llaves descifradas y aplicadas para el medidor.");

        }
        catch (const std::exception& e) {
            spdlog::error("ApplyClientProfile: Error descifrando llaves para el medidor: {}", e.what());
            // Manejar el error: ¿seguir sin cifrado? ¿abortar conexión?
            // Lo más seguro es abortar.
            throw;
        }
    }
}

boost::asio::awaitable<void> Session::WriteLogAsync(const std::string& stringlog, int server)
{
    co_await boost::asio::post(boost::asio::system_executor(), boost::asio::use_awaitable);
    WriteLog(stringlog, server); // se ejecuta en hilo del pool
    co_return;
}

void Session::SaveDataFromProfile_Core()
{
    std::string dat, res, sqlvalues, restotal, sqlvalues_batch;
    int error = 0;
    if (m_modereadprofile == 1)
    {
        try
        {
            if (m_dataprofile.size() > 0)
            {
                size_t rows = m_dataprofile.size();
                size_t column = m_dataprofile[0].size();
                //printf("rows %d,cols %d,scaler %d\n", rows, column,(int)scalerobjects.size());
                for (size_t i = 0; i < rows; i++)
                {
                    if (m_rowid == 1)
                    {
                        m_dataprofile[i].erase(m_dataprofile[i].begin());
                        column = m_dataprofile[i].size();
                    }
                    //printf("savedataformfile cols:%d\n", (int)m_dataprofile[i].size());
                    for (size_t j = 0; j < column; j++)
                    {
                        dat = m_dataprofile[i][j];
                        // printf("savedatafromfile data for %d,%d \n",i,j);
                        if (j > (m_ScalerObjects.size()))
                        {
                            printf("ERROR: ***** Datos extras en solicitud **** \n");
                            error = 1;
                            break;
                        }
                        else
                        {
                            //printf("savedatafromfile scale for \n");
                            if (ltrim(m_arrayobjectsobis[j]) == "0.0.1.0.0.255")
                            {
                                // dat = DateToString("%Y-%m-%d %H:%M:%S", datos[i][j].dateTime.GetValue()); no aplica escala
                            }
                            else
                            {
                                //printf("construir dat:%s,%s\n",dat.c_str(),scalerobjects[j-1].c_str());
                                dat = std::to_string(atof(dat.c_str()) * atof(m_ScalerObjects[j - 1].c_str()));
                            }

                            error = 0;
                        }
                        if (m_relecturas == 0)
                        {
                            res += dat + ";";
                        }
                        else
                        {
                            res += dat + ",";
                        }
                    }
                    res = res.substr(0, res.length() - 1);
                    if (m_relecturas == 1 && error == 0)
                    {
                        sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_metercode + "," + res + "}";
                        sqlvalues_batch += sqlvalues + ",";
                        //execute_sql_ff(AmiQueries::SAVEMETERDATA, sqlvalues);
                    }
                    else
                    {
                        printf("No se puede guardar valores\n");
                    }
                    res.clear();
                    sqlvalues.clear();
                    if (m_relecturas == 0)
                    {
                        restotal += res + "|";
                        res.clear();
                    }
                }
                if (m_relecturas == 0)
                {
                    restotal = restotal.substr(0, restotal.length() - 1);
                    sqlvalues = "{" + std::to_string(m_idcomando) + "," + restotal + "}";
                    execute_sql_ff(AmiQueries::SAVEMETERRESPONSE, sqlvalues);
                    restotal.clear();
                }
                else
                {
                    sqlvalues_batch = sqlvalues_batch.substr(0, sqlvalues_batch.length() - 1);
                    sqlvalues_batch = "{" + sqlvalues_batch + "}";
                    execute_sql_ff(AmiQueries::SAVEMETERDATABULK,  sqlvalues_batch, std::to_string(m_idcomando) );
                    sqlvalues_batch.clear();
                }
                if (error != 0)
                {
                    UpdateCommandState(-2);//perfil diferente
                }
            }
            else
            {
                UpdateCommandState(-1);//sin datos de perfil
            }


        }
        catch (const std::exception& e)
        {
            printf("Exception %s\n", e.what());
            UpdateCommandState(-255);

        }

    }
    if (m_modereadprofile == 0) //original, como se venia manejando
    {
        std::vector<std::string>vecdatos;
        std::vector<std::vector<CGXDLMSVariant >> datos;
        datos = m_objprofile->GetBuffer();
        std::vector<std::string> values2;
        m_objprofile->GetValues(values2);
        char tmpfecha[256];
        std::string fecha;
        std::string sqlvalues_batch;
        if (datos.size() > 0)
        {
            for (size_t i = 0; i < datos.size(); i++)//filas
            {
                if (m_rowid == 1)
                {
                    datos[i].erase(datos[i].begin());
                }
                //printf("%s:Begin Data Profile(0)\n", m_codigomedidor.c_str());
                for (size_t j = 0; j < datos[i].size(); ++j)//numero de datos que llegan columnas
                {
                    if (j == 0)//la fecha
                    {
                        //StringToDate(datos[i][j].dateTime.GetValue())
                        if (m_typeclcosem == 0)//itron
                        {
                            //{07 E5 09 0A FF FF FF FF FF 80 00 FF, 000000}//datos[i][j].ToString().;
                            if (datos[i][j].Arr.size() > 0)
                            {

                                CGXDLMSVariant rawdate = datos[i][j].Arr[0];
                                std::string rawfecha = ByteToString(rawdate.byteArr, 0, 2);
                                int anio = (int)std::stoul(rawfecha, nullptr, 16);
                                rawfecha = ByteToString(rawdate.byteArr, 2, 1);
                                int mes = (int)std::stoul(rawfecha, nullptr, 16);
                                rawfecha = ByteToString(rawdate.byteArr, 3, 1);
                                int dia = (int)std::stoul(rawfecha, nullptr, 16);
                                sprintf(tmpfecha, "%d-%02d-%02d 00:00:00", anio, mes, dia);
                                fecha = tmpfecha;// std::to_string(anio) + "-" + std::to_string(mes) + "-" + std::to_string(dia);
                                //printf("fecha %s\n", fecha.c_str());

                            }
                            else
                            {
                                if (!fecha.empty())
                                {
                                    fecha = AddMinuteToDate(fecha, 15);
                                }

                            }
                        }
                        else
                        {
                            if (ltrim(m_arrayobjectsobis[j]) == "0.0.1.0.0.255")
                            {
                                dat = DateToString("%Y-%m-%d %H:%M:%S", datos[i][j].dateTime.GetValue());
                            }
                            else
                            {
                                dat = std::to_string(atof(dat.c_str()) * atof(m_ScalerObjects[j].c_str()));
                            }
                            /*std::vector<std::string>::iterator it;
                            it = find(m_arrayobjectsobis.begin(), m_arrayobjectsobis.end(), "0.0.1.0.0.255");
                            if (it != m_arrayobjectsobis.end())
                            {
                                dat = DateToString("%Y-%m-%d %H:%M:%S", datos[i][j].dateTime.GetValue());//encontrado
                            }
                            else
                            {
                            }
                                //std::cout << "Element not found in myvector\n";
                            */


                        }
                    }
                    else
                    {
                        if (m_typeclcosem == 0)//itron
                        {
                            if (j >= 1 && j <= 3)
                            {
                                //no lo guarda
                            }
                            else
                            {
                                dat = datos[i][j].ToString();
                                dat = std::to_string(atof(dat.c_str()) * atof(m_ScalerObjects[j - 1].c_str()));
                            }
                        }
                        else
                        {
                            if (ltrim(m_arrayobjectsobis[j]) == "0.0.1.0.0.255")
                            {
                                dat = DateToString("%Y-%m-%d %H:%M:%S", datos[i][j].dateTime.GetValue());
                            }
                            else
                            {
                                dat = datos[i][j].ToString();
                                if (j > (m_ScalerObjects.size()))
                                {
                                    printf("ERROR: ***** Datos extras en solicitud **** \n");
                                    error = 1;
                                    break;
                                }
                                else
                                {
                                    dat = std::to_string(atof(dat.c_str()) * atof(m_ScalerObjects[j - 1].c_str()));
                                    error = 0;
                                }

                            }
                        }
                    }
                    if (m_relecturas == 0)
                    {
                        res += dat + ";";
                    }
                    else
                    {
                        res += dat + ",";
                    }

                }
                res = res.substr(0, res.length() - 1);
                if (m_relecturas == 1 && error==0)
                {
                    sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_metercode + "," + res + "}";
                    sqlvalues_batch += sqlvalues + ",";
                    //execute_sql(AmiQueries::SAVEMETERDATA, sqlvalues);
                }
                else
                {
                    printf("No se puede guardar valores\n");
                }
                res.clear();
                sqlvalues.clear();
                if (m_relecturas == 0)
                {
                    restotal += res + "|";
                    res.clear();
                }
            }
            if (m_relecturas == 0)
            {
                restotal = restotal.substr(0, restotal.length() - 1);
                sqlvalues = "{" + std::to_string(m_idcomando) + "," + restotal + "}";
                execute_sql_ff(AmiQueries::SAVEMETERREQUEST, sqlvalues);
                restotal.clear();
            }
            else
            {
                sqlvalues_batch = sqlvalues_batch.substr(0, sqlvalues_batch.length() - 1);
                sqlvalues_batch = "{" + sqlvalues_batch + "}";
                cout << sqlvalues_batch << std::endl;
                execute_sql_ff(AmiQueries::SAVEMETERDATABULK, sqlvalues_batch, std::to_string(m_idcomando) );//aqui mismo actualiza a estado 5
                sqlvalues_batch.clear();
            }
            if (error != 0)
            {
                UpdateCommandState(-2);//perfil diferente
            }
        }
        else
        {
            if (values2[1] == "" && values2[2].size() > 16)
            {
                //printf("%s sin datos en perfil \n", m_codigomedidor.c_str());
                UpdateCommandState(-1);
            }
            else
            {
                //printf("%s peticion perfil 1 FAIL\n ", m_codigomedidor.c_str());
                UpdateCommandState(0);
            }

        }
    }

}

void Session::SavedataFromProfileAuto_Core()
{
    std::string sqlvalues,sqlvalues_batch;
    std::vector<std::vector<CGXDLMSVariant >> datos;
    datos = m_objprofile->GetBuffer();
    std::vector<std::string> values2;
    m_objprofile->GetValues(values2);

    int error = 0;
    std::string res, dat;
    
    try
    {
        for (size_t i = 0; i < datos.size(); i++)
        {
            for (size_t j = 0; j < datos[i].size(); ++j)
            {
                if (j == 0)//la fecha
                {
                    if (ltrim(m_arrayobjectsobis[j]) == "0.0.1.0.0.255")
                    {
                        dat = DateToString("%Y-%m-%d %H:%M:%S", datos[i][j].dateTime.GetValue());
                    }
                    else
                    {
                        dat = std::to_string(atof(dat.c_str()) * atof(m_ScalerObjects[j - 1].c_str()));
                    }
                }
                else
                {
                    if (ltrim(m_arrayobjectsobis[j]) == "0.0.1.0.0.255")
                    {
                        dat = DateToString("%Y-%m-%d %H:%M:%S", datos[i][j].dateTime.GetValue());
                    }
                    else
                    {
                        dat = datos[i][j].ToString();
                        if (j > (m_ScalerObjects.size()))
                        {
                            printf("ERROR: ***** Datos extras en solicitud **** \n");
                            error = 1;
                            break;
                        }
                        else
                        {
                            dat = std::to_string(atof(dat.c_str()) * atof(m_ScalerObjects[j - 1].c_str()));
                            //printf("%s:data,scale: %s,%s\n", m_codigomedidor.c_str(), dat.c_str(), scalerobjects[j - 1].c_str());
                            error = 0;
                        }
                    }
                }
                res += dat + ",";
            }
            res = res.substr(0, res.length() - 1);
            //res.pop_back();
            if (error == 0)
            {
                sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_metercode + "," + res + "}";
                sqlvalues_batch += sqlvalues + ",";
                execute_sql_ff(AmiQueries::SAVEMETERDATA, sqlvalues);
            }
            res.clear();
            sqlvalues.clear();
            m_statusprofile = 5;
        }
        if (error == 0)
        {
            sqlvalues_batch.pop_back();
            sqlvalues_batch = "{" + sqlvalues_batch + "}";
            execute_sql_ff(AmiQueries::SAVEMETERDATABULK, sqlvalues_batch, m_idcomando);
            sqlvalues_batch.clear();
        }

    }
    catch (const std::exception& e)
    {
        std::cerr << "error en SavedatafromPrfileauto " << e.what();
    }
}

void Session::UpdateCommandState(int estado)
{
    if (m_idcomando > 0)
    {
        execute_sql_ff(AmiQueries::UPDATECOMMANDSTATUS, m_idcomando, estado);
    }
}

int Session::Disconnect()
{
    int ret;
    std::vector<CGXByteBuffer> data;
    ret = m_Parser->DisconnectRequest(data);
    if (ret != 0)
    {
        printf("DisconnectRequest failed (%d) %s.\n", ret, CGXDLMSConverter::GetErrorMessage(ret));
    }
    //ReadDataBlock(data);//opcion 1
    for (auto& f : data) {
        ReadDLMSPacket(f);
    }
    m_dlms_state = ami::DlmsState::DisconnectSent;
    return 0;
}

void Session::UpdateDBMeterEstatus(int status)
{
    //std::string sql = "func_updatemeterstatus($1,$2)";
    //basedatos.SConsulta(sql, { m_codigomedidor,std::to_string(status) });
    execute_sql_ff(AmiQueries::UPDATEMETERSTATUS, m_metercode, std::to_string(status));
    /*
    asio::co_spawn(socket_.get_executor(),
        [self = shared_from_this(),status]() -> boost::asio::awaitable<void> {
            try {
                // Ejecuta la consulta asincrónicamente sin resultados
                co_await self->execute_query_async_coro(AmiQueries::UPDATEMETERSTATUS, self->m_codigomedidor, std::to_string(status) );
            }
            catch (const std::exception& ex) {
                std::cerr << "Error en UpdateDBMeterEstatus: " << ex.what() << std::endl;
            }

            co_return;
        }, boost::asio::detached);
        */
}

void Session::GetIdMeter()
{
    if (m_initialization == 0)
    {
        if (m_metercode.empty() || m_metercode == "")
        {
            CGXDLMSData* obj = NULL;
            int port = 0;// (int)GetSockPort();

            if (port == m_portmacrostar)//macros star sin authenticacion
            {
                obj = new CGXDLMSData("0.0.42.0.0.255");
            }
            else //para cualquiera y los que requieran con autenticacion
            {
                obj = new CGXDLMSData("0.0.96.1.0.255");
            }

            if (obj != NULL)
            {
                auto Frames = Read(obj, 2);
                for (auto& f : Frames) {
                    ReadDLMSPacket(f);
                }
                /*if ((ret = Read(obj, 2)) != DLMS_ERROR_CODE_OK)
                {
                    printf("Error! Index: 2 read failed: %s\r\n", CGXDLMSConverter::GetErrorMessage(ret));
                }
                else
                {
                    //respuesta de lectura ok
                    //printf("for : %d\n", c);
                }
                */

                m_dlms_state = ami::DlmsState::GetMeterIdResp;

            }
            else
            {
                printf("Unknown object:\n");
            }
        }
    }
}

int Session::SendObjDlms(CGXDLMSObject* pObject, int attributeIndex)
{
    int ret;
    m_pObject = pObject;
    m_attributeIndex = attributeIndex;
    std::vector<CGXByteBuffer> data;
    CGXReplyData reply;
    //Read data from the meter.
    ret = m_Parser->Read(pObject, attributeIndex, data);
    std::string str = GXHelpers::BytesToHex(data[0].GetData(), (int)data[0].GetSize(), false);
    //printf("TX: %s\n", str.c_str());
    ret = ReadDataBlock(data);
    if (ret != 0)
    {
        //hubo un error
    }
    return DLMS_ERROR_CODE_OK;
}

void Session::SetClientCosem(int clientadd, std::string authkey, std::string cipherblockkey, std::string dedicatekey)
{
    m_Parser->SetClientAddress((unsigned long)clientadd);
    m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_INFORMATION_REPORT | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION));
    m_Parser->SetNegotiatedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_INFORMATION_REPORT | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION));
    
    m_Parser->SetAuthentication(DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_HIGH_GMAC);
    CGXByteBuffer bb;
    if (!authkey.empty() && !cipherblockkey.empty())
    {
#if _DEBUG_
        bb.SetHexString(m_authenticationkey);
#else
        //bb.SetHexString(static_cast<std::string>(m_authenticationkey));
        bb.SetHexString(m_authenticationkey);
#endif

        m_Parser->GetCiphering()->SetAuthenticationKey(bb);
    }
    if (!cipherblockkey.empty())
    {
        bb.Clear();

#if _DEBUG_
        bb.SetHexString(m_cipherkey);
#else
        //bb.SetHexString(static_cast<std::string>(m_cipherkey));
        bb.SetHexString(m_cipherkey);
#endif
        m_Parser->GetCiphering()->SetBlockCipherKey(bb);
    }
    if (!dedicatekey.empty())
    {
        bb.Clear();
#if _DEBUG_
        bb.SetHexString(dedicatekey);
#else
        //bb.SetHexString(static_cast<std::string>(m_cipherkey));
        bb.SetHexString(dedicatekey);
#endif
        m_Parser->GetCiphering()->SetDedicatedKey(bb);
    }
    bb.Clear();
    bb.AddString("IGRID123");//i-GRID22
    m_Parser->GetCiphering()->SetSystemTitle(bb);

    m_Parser->GetCiphering()->SetSecurity(DLMS_SECURITY::DLMS_SECURITY_AUTHENTICATION_ENCRYPTION);
    m_Parser->GetCiphering()->SetSecuritySuite(DLMS_SECURITY_SUITE::DLMS_SECURITY_SUITE_V0);
    m_Parser->SetAutoIncreaseInvokeID(true);
}

void Session::SendBuffer(std::vector<unsigned char> data)
{
    auto self(this->shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(data.data(), data.size()),
        [this, self, data = std::move(data)](std::error_code ec, std::size_t) {
            if (!ec) {
                //std::cout << "[OK]" << "\n";

            }
            else {
                std::cout << "Error enviando a " << m_metercode << "\n";
            }
        });
}

void Session::SendInvocationCounter()
{
    if (m_initialization != 0 || !m_Parser)
        return;

    CGXDLMSData counterObject("0.0.43.1.0.255");
    std::vector<CGXByteBuffer> frames;
    const int ret = m_Parser->Read(&counterObject, 2, frames);
    if (ret != DLMS_ERROR_CODE_OK || frames.empty()) {
        spdlog::error("[{}] No se pudo construir la lectura del Invocation Counter ({})", m_metercode, ret);
        return;
    }

    m_dlms_state = ami::DlmsState::InvocationCounterResp;
    for (auto& frame : frames)
        ReadDLMSPacket(frame);
}

boost::asio::awaitable<int> Session::SendInvocationCounterMeter()
{
    if (m_initialization != 0 || !m_Parser)
        co_return DLMS_ERROR_CODE_FALSE;

    CGXDLMSData counterObject("0.0.43.1.0.255");
    std::vector<CGXByteBuffer> frames;
    const int ret = m_Parser->Read(&counterObject, 2, frames);
    if (ret != DLMS_ERROR_CODE_OK || frames.empty()) {
        spdlog::error("[{}] No se pudo construir la lectura del Invocation Counter ({})", m_metercode, ret);
        co_return ret != DLMS_ERROR_CODE_OK ? ret : DLMS_ERROR_CODE_FALSE;
    }

    m_dlms_state = ami::DlmsState::InvocationCounterResp;
    co_return co_await ReadDataBlockAsync(std::move(frames));
}

boost::asio::awaitable<int> Session::DisconnectAsync()
{
    if (!m_Parser) co_return DLMS_ERROR_CODE_FALSE;

    std::vector<CGXByteBuffer> frames;
    const int ret = m_Parser->DisconnectRequest(frames);
    if (ret != DLMS_ERROR_CODE_OK || frames.empty()) {
        spdlog::error("[{}] No se pudo construir DisconnectRequest ({})", m_metercode, ret);
        co_return ret != DLMS_ERROR_CODE_OK ? ret : DLMS_ERROR_CODE_FALSE;
    }

    m_dlms_state = ami::DlmsState::DisconnectSent;
    co_return co_await ReadDataBlockAsync(std::move(frames));
}

void Session::ProcessComandInit_async()
{
    if (m_concentrator.empty())
    {
        return;
    }
    if(m_inprogress != 0)
    {
        return;
    }

    // 1. Preparamos la consulta y definimos el tipo de resultado.
    // La tupla debe coincidir con las 11 columnas que devuelve tu función de BD.
    boost::asio::co_spawn(socket_.get_executor(),
        [self = shared_from_this()]()->boost::asio::awaitable<void> {
            try
            {
                auto res = co_await self->query(AmiQueries::GETCOMMANDDCU, { self->m_concentrator });
                if (res.countRows() > 0) {
                    std::string opc = res.get(0, 0);
                    self->m_metercode = res.get(0, 6);
                    self->m_relecturas = atoi(res.get(0, 5).c_str());
                    //self->m_typecommand = self->m_comands[opc];
					std::string code = self->resolveAlias(opc);
					self->m_typecommand = ami::resolveProtocolCode(code);
					if (self->m_typecommand == ami::CommandType::Unknown)
					{
						spdlog::error("Unknown command code: {} (resolved from alias '{}')", opc, code);
						co_return;
					}
                    self->m_idcomando = atol(res.get(0, 3).c_str());
                    self->m_accion = res.get(0, 4);
                    self->m_fechaini = res.get(0, 1);
                    self->m_fechafin = res.get(0, 2);
                    self->m_OBISobjects.clear();
                    self->m_ScalerObjects.clear();
                    Split(res.get(0, 7).c_str(), ',', &self->m_OBISobjects);
                    Split(res.get(0, 8).c_str(), ',', &self->m_ScalerObjects);
                    self->m_subtipo = (int)atol(res.get(0, 9).c_str());//para determinar si es macro o medidor virtual
                    self->m_numrepeticion = (int)atol(res.get(0, 10).c_str());//para determinar el numero de veces que se intento el comando
					if (self->m_fabricante == 3) {
						if (ami::isRfCommand(self->m_typecommand)) {
							spdlog::error("[{}] Comando RF no soportado por el flujo DLMS ZStar", self->m_concentrator);
							self->UpdateCommandState(-1);
							co_return;
						}
						if (self->m_subtipo != 1 && self->m_subtipo != 2) {
							spdlog::error("[{}] Subtipo ZStar no soportado: {}", self->m_concentrator, self->m_subtipo);
							self->UpdateCommandState(-1);
							co_return;
						}
						if (self->m_inprogress == 0)
							co_await self->InitConnectionZstarAsync();
					}
					else if (ami::isRfCommand(self->m_typecommand))
					{
						if (self->m_inprogress == 0)
						{
							self->ProcessComand(self->m_typecommand);
						}
					}
					else
					{
						if (self->m_subtipo == 0)//macrodcu
						{
                            if (self->m_inprogress == 0)
                            {
                                self->m_initialization = 0;
                                self->m_typeinterface = 0;
                                self->m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 0x12, 1);
                                self->m_Parser->SetServiceClass(DLMS_SERVICE_CLASS::DLMS_SERVICE_CLASS_CONFIRMED);
                                self->m_metertype = 0;
                                self->m_inprogress = 1;
                                const int ret = co_await self->SnrmRequest();
                                if (ret != DLMS_ERROR_CODE_OK) {
                                    self->m_initialization = -1;
                                    self->m_dlms_state = ami::DlmsState::Idle;
                                    self->m_inprogress = 0;
                                    self->UpdateCommandState(-1);
                                }
                            }

                        }
                        else //meter
                        {
                            if (self->m_inprogress == 0)
                            {
                                //DLMS_INTERFACE_TYPE tp = m_Parser->GetInterfaceType();
                                co_await self->InitConnectionMeterDcuINH();

                            }
                        }
                    }
                    
                }
                co_return;

            }
            catch (const std::exception& ex)
            {
                std::cerr << "error: " << ex.what() << std::endl;
                co_return;
            }
        },
        boost::asio::detached);
}

void Session::ProcessComand(ami::CommandType opc)
{
	auto it = m_dispatch.find(opc);
	if (it == m_dispatch.end())
	{
		spdlog::error("Error: Unknown command type: {}", static_cast<int>(opc));
        return;
	}
	
	it->second(*this);
    /*
    switch (opc)
    {
    case 0://valores instantaneos
    {
        //if (m_inprogress == 0)
        {
            UpdateCommandState(1);
            SendInstantaneousValues();
        }
        break;
    }
    case 1://profile generic :historicos horarios
    {
        int profilechanel = 0;
        int numperfil = 0;
        //if (m_inprogress == 0)
        {
            //m_idcomando = atol(resp.get(0, 3).c_str());
            UpdateCommandState(1);
            std::vector<std::string> acciones;

            //datos del vector profilechannel,accion,perfil
            if (m_accion.empty())
            {
                profilechanel = 0;
            }
            else
            {
                Explode(m_accion, ",", &acciones);
                if (acciones.size() == 1)
                {
                    profilechanel = atoi(acciones[0].c_str());
                }
                else
                {
                    profilechanel = atoi(acciones[0].c_str());
                    numperfil = atoi(acciones[1].c_str());

                }
                m_numperfil = numperfil;
            }
            SendProfileGeneric(m_fechaini, m_fechafin, profilechanel);
        }
        break;
    }
    case 2://reconexiones y suspensiones
    {
        //if (m_inprogress == 0)
        {
            //m_idcomando = atol(resp.get(0, 3).c_str());
            UpdateCommandState(1);
            int opc = atoi(m_accion.c_str());
            DisconnectControl(opc);
        }
        break;
    }
    case 5://cambio de perfil
    {
        //if (m_inprogress == 0)
        {
            //m_idcomando = atol(resp.get(0, 3).c_str());
            UpdateCommandState(1);
            std::vector<std::string> acciones;
            Explode(m_accion, "-", &acciones);
            //obis1,obis2,obis3,.....obisx-perfil
            //codigos obis de perfil,numero de perfil o canal
            if (acciones.size() != 2)
            {
                UpdateCommandState(-3);//bloque de acciones mal formado
            }
            else
            {
                WriteProfileGeneric(acciones[0], atoi(acciones[1].c_str()));
            }
        }
        break;
    }
    case 6://cambio de periodo de tiempo de perfil
    {
        //if (m_inprogress == 0)
        {
            //m_idcomando = atol(resp.get(0, 3).c_str());
            UpdateCommandState(1);
            std::vector<std::string> acciones;
            Explode(m_accion, "-", &acciones);
            //periodo de captura,numero de perfil
            if (acciones.size() != 2)
            {
                UpdateCommandState(-3);//bloque de acciones mal formado
            }
            else
            {
                WritePeriodProfileGeneric(acciones[0], atoi(acciones[1].c_str()));
            }
        }
        break;
    }
    case 7://comando personalizado segun obis
    {
        //if (m_inprogress == 0)
        {
            //m_idcomando = atol(resp.get(0, 3).c_str());
            UpdateCommandState(1);
            std::vector<std::string> acciones;
            Explode(m_accion, ",", &acciones);
            //typo de objeto,obis,attindex
            if (acciones.size() != 3)
            {
                UpdateCommandState(-3);//bloque de acciones mal formado
            }
            else
            {
                ReadObis(atoi(acciones[0].c_str()), acciones[1], atoi(acciones[2].c_str()), 7);
            }

        }
        break;
    }
    case 8://validador de medidor con ip
    {
        //if (m_inprogress == 0)
        {
            //m_idcomando = atol(resp.get(0, 3).c_str());
            UpdateCommandState(1);
            std::vector<std::string> acciones;
            Explode(m_accion, ",", &acciones);
            //typo de objeto,obis,attindex
            if (acciones.size() > 0)
            {
                if (acciones.size() != 3)
                {
                    UpdateCommandState(-3);//bloque de acciones mal formado
                }
                else
                {
                    ReadObis(atoi(acciones[0].c_str()), acciones[1], atoi(acciones[2].c_str()), 8);
                }

            }
            else
            {
                ReadObis(1, "1.0.0.0.0.255", 2, 8);
            }

        }
        break;
    }
    case 9://profile generic: eventos en general
    {
        int profilechanel = 0;
        //int numperfil = 0;
        //if ((m_inprogress == 0) && (m_initialization == 0))
        //{
            //m_idcomando = atol(resp.get(0, 3).c_str());
        UpdateCommandState(1);
        std::vector<std::string> acciones;

        //datos del vector profilechannel,accion,perfil
        if (m_accion.empty())
        {
            profilechanel = 0;
        }
        else
        {
            Explode(m_accion, ",", &acciones);//canalperfil,numeroperfil
            if (acciones.size() == 1)
            {
                profilechanel = atoi(acciones[0].c_str());
            }
            else
            {
                profilechanel = atoi(acciones[0].c_str());
                //numperfil = atoi(acciones[1].c_str());
            }
            m_numperfil = profilechanel;
            //profilechanel = atoi(resp.get(0, 4).c_str());
        }
        SendProfileGenericBase(m_fechaini, m_fechafin, profilechanel);
        //}
        break;
    }
    case 10://comando obis personalizado(escritura)
    {
        UpdateCommandState(1);
        std::vector<std::string> acciones, obis, values;
        Explode(m_accion, "-", &acciones);
        //typo de objeto,obis,attindex-value
        if (acciones.size() > 1)
        {
            Explode(acciones[0], ",", &obis);
            values[0] = acciones[1]; //por ahora values solo acepta un parametro
        }
        else
        {
            Explode(acciones[0], ",", &obis);
            values.push_back("");
        }
        if (((int)acciones.size() < 0) || ((int)acciones.size() > 2))
        {
            UpdateCommandState(-3);//bloque de acciones mal formado
        }
        else
        {
            WriteObis(atoi(obis[0].c_str()), obis[1], atoi(obis[2].c_str()), 3, values[0]);
        }
        break;
    }
    case 100:
    {
        UpdateCommandState(1);
        std::vector<unsigned char> datos = m_gdw.StopReadingTask(m_concentrator);
        std::string tmp = "";
        tmp = m_metercode + "(RF1) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        
        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 101:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.ClearMeasurementPoint(m_concentrator);
        std::string tmp = "";
        tmp = m_metercode + "(RF2) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF2): %s\n", GXHelpers::BytesToHex(datos.data(),(int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 102:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.ClearTask(m_concentrator);
        std::string tmp = "";

        tmp = m_metercode + "(RF3) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF3): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());


        //SendBuf((const char*)datos.data(), (int)datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 103:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.F33(m_concentrator);
        std::string tmp = "";

        tmp = m_metercode + "(RF4) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF4): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());


        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 104:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.F38(m_concentrator);
        std::string tmp = "";

        tmp = m_metercode + "(RF5) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF5): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 105:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.F39(m_concentrator);
        std::string tmp = "";

        tmp = m_metercode + "(RF6) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF6): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 106:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.F40(m_concentrator);
        std::string tmp = "";

        tmp = m_metercode + "(RF7) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF7): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    //devuelven dato de respuesta
    case 107:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.ReadRfVersion(m_concentrator);
        std::string tmp = "";

        tmp = m_metercode + "(RF8) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF8): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 108:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.ReadNetworkParameter(m_concentrator);
        std::string tmp = "";

        tmp = m_metercode + "(RF9) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF9): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 109:
    {
        std::vector<std::string> acciones;
        Explode(m_accion, ",", &acciones);
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.SetNetworkParameter(m_concentrator, atoi(acciones[0].c_str()), atoi(acciones[1].c_str()));
        std::string tmp = "";

        tmp = m_metercode + "(RF10) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF10): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 110:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.AddNetPoint(m_concentrator, m_accion);
        std::string tmp = "";

        tmp = m_metercode + "(RF11) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF11): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 111:
    {
        std::vector<std::string> acciones;
        Explode(m_accion, ",", &acciones);
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.SetMeasurementPoint(m_concentrator, acciones[0], atoi(acciones[1].c_str()), atoi(acciones[2].c_str()), atoi(acciones[3].c_str()));
        std::string tmp = "";

        tmp = m_metercode + "(RF12) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF12): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 112:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.AddBroadcastPointNumber(m_concentrator, m_accion);
        std::string tmp = "";

        tmp = m_metercode + "(RF13) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF13): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 113:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.StartRfNetworking(m_concentrator);
        std::string tmp = "";

        tmp = m_metercode + "(RF14) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF14): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 114:
    {
        std::vector<std::string> acciones;
        Explode(m_accion, ",", &acciones);
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.ReadNodeLevel(m_concentrator, atoi(acciones[0].c_str()), atoi(acciones[1].c_str()));
        std::string tmp = "";

        tmp = m_metercode + "(RF15) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF15): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    case 115:
    {
        UpdateCommandState(1);
        std::vector<unsigned char>datos = m_gdw.StopBuild(m_concentrator);
        std::string tmp = "";
        tmp = m_metercode + "(RF16) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
        WriteLog(tmp + "\n", 3);
        //printf("TX(RF16): %s\n", GXHelpers::BytesToHex(datos.data(), (int)datos.size(),false).c_str());

        //SendBuf((const char*)datos.data(), datos.size());//verificar si manda el error
        //flagdlms = 11;
        m_inprogress = 1;
        break;
    }
    default:
        printf("Comando desconocido\n");
        break;
    }*/
}

void Session::start_timer_dcu()
{
    m_timer_dcu.expires_after(std::chrono::seconds(5));
    m_timer_dcu.async_wait([self=shared_from_this()/*this*/](std::error_code ec) {
        if (!ec) {
            //aqui procesa comandos
            //ProcessComand();
            self->ProcessComandInit_async();
            self->start_timer_dcu();
        }
        });
}

boost::asio::awaitable<void> Session::InitConnectionMeterDcuINH()
{
    m_inprogress = 1;
    const std::string sqlvalues = "{" + m_metercode + ",InitializeConnection,0}";
    execute_sql_ff(AmiQueries::CALL_MONITOR, sqlvalues);

    m_typeclcosem = 6;
    co_await InitClientCosemAsync();
    if (!m_Parser) {
        spdlog::error("[{}] No se pudo crear el cliente DLMS para medidor detrás de DCU {}",
            m_concentrator, m_metercode);
        m_initialization = -1;
        m_inprogress = 0;
        UpdateCommandState(-1);
        co_return;
    }

    m_timecommand = time(nullptr);
    std::vector<CGXByteBuffer> frames;
    SNRMRequestINH(frames);
    if (frames.empty()) {
        spdlog::error("[{}] No se pudo construir SNRM para medidor {}", m_concentrator, m_metercode);
        m_initialization = -1;
        m_inprogress = 0;
        UpdateCommandState(-1);
        co_return;
    }

    m_dlms_state = ami::DlmsState::SnrmSent;
    for (auto& frame : frames) {
        const int ret = co_await ReadDLMSPacketAsync(std::move(frame));
        if (ret != DLMS_ERROR_CODE_OK) {
            spdlog::error("[{}] Falló el envío de SNRM a medidor {} ({})", m_concentrator, m_metercode, ret);
            m_dlms_state = ami::DlmsState::Idle;
            m_initialization = -1;
            m_inprogress = 0;
            UpdateCommandState(-1);
            co_return;
        }
    }
}

void Session::SNRMRequestINH(std::vector<CGXByteBuffer>& packets)
{
    CGXByteBuffer data(25);
    data.SetUInt8(0x81);
    // GroupID
    data.SetUInt8(0x80);
    // Length is updated later.
    data.SetUInt8(0);
    data.SetUInt8(HDLC_INFO_MAX_INFO_TX);
    CGXDLMS::AppendHdlcParameter(data, 230);
    data.SetUInt8(HDLC_INFO_MAX_INFO_RX);
    CGXDLMS::AppendHdlcParameter(data, 230);
    data.SetUInt8(HDLC_INFO_WINDOW_SIZE_TX);
    data.SetUInt8(4);
    data.SetUInt32(0x01);
    data.SetUInt8(HDLC_INFO_WINDOW_SIZE_RX);
    data.SetUInt8(4);
    data.SetUInt32(0x01);
    // Length.
    data.SetUInt8(2, (unsigned char)(data.GetSize() - 3));
    CGXDLMSSettings m_Settings(true);
    m_Settings.SetClientAddress(0x01);
    m_Settings.SetServerAddress(0x01);
    m_Settings.ResetFrameSequence();
    CGXByteBuffer reply2;
    CGXDLMS::GetHdlcFrame(m_Settings, DLMS_COMMAND_SNRM, &data, reply2);
    packets.push_back(reply2);
}

void Session::SendInstantaneousValues()
{
    //int ret;
    if (m_initialization == 0)
    {
        CGXDLMSRegister* obj = new CGXDLMSRegister(m_OBISobjects[m_indexiv]);
        printf("obis: %s\n", m_OBISobjects[m_indexiv].c_str());
        //obj->SetScaler(0.001);
        if (obj != NULL)
        {
            auto aarqFrames = Read(obj, 2);
            for (auto& f : aarqFrames) {
                ReadDLMSPacket(f);
            }
            UpdateCommandState(2);
            /*if ((ret = Read(obj, 2)) != DLMS_ERROR_CODE_OK)
            {
                printf("Error! Index: 2 read failed: %s\r\n", CGXDLMSConverter::GetErrorMessage(ret));
                m_inprogress = 0;
                UpdateCommandState(0);
            }
            else
            {
                m_inprogress = 1;
                UpdateCommandState(2);
            }*/
        }
        else
        {
            printf("Unknown object: %s", m_OBISobjects[m_indexiv].c_str());
            //comm.WriteValue(GX_TRACE_LEVEL_ERROR, str);
        }
        m_dlms_state = ami::DlmsState::InstantaneousResp;
    }
}

void Session::ResponseInstantaneousValues()
{
    std::string valuefromobject;
    m_reply.Clear();
    //pObject = m_pObject;
    //attributeIndex = m_attributeIndex;
    int ret = m_Parser->UpdateValue(*m_pObject, m_attributeIndex, m_reply.GetValue());
    //Update data type.
    DLMS_DATA_TYPE type;
    if ((ret = m_pObject->GetDataType(m_attributeIndex, type)) != 0)
    {
        //return ret; retorna error
    }
    if (type == DLMS_DATA_TYPE_NONE)
    {
        type = m_reply.GetValue().vt;
        if ((ret = m_pObject->SetDataType(m_attributeIndex, type)) != 0)
        {
            //return ret; retorna error
        }
    }
    std::vector<std::string> values;
    m_pObject->GetValues(values);
    valuefromobject = values[m_attributeIndex - 1];



    m_instantaneousvalues.push_back(valuefromobject);
    if (m_indexiv < (int)(m_OBISobjects.size() - 1))
    {
        m_indexiv++;
        SendInstantaneousValues();
        UpdateCommandState(3);
    }
    else
    {
        if (m_indexiv == (int)(m_OBISobjects.size() - 1)) //se pidieron todos los objetos del instantaneousvalues
        {
            m_indexiv = 0;
            if (m_instantaneousvalues.size() == m_OBISobjects.size())
            {
                //explode el vector : medidor,fecha,datos..... debe quedar {a,b,c,d,e,f,g}
                //guardar el dato en la base de datos
                std::vector<std::string>valuesvi;

                std::transform(m_instantaneousvalues.begin(), m_instantaneousvalues.end(), m_ScalerObjects.begin(),
                    std::back_inserter(valuesvi), [](std::string value, std::string scale)
                    {return std::to_string(atof(value.c_str()) * atof(scale.c_str())); });

                std::string sqlvalues;
                if (m_idcomando > 0)
                {
                    //es una peticion hecha por el usuario, por tanto se guarda como respuesta
                    //update peticionesxx set respuesta where idcomando=x
                    if (m_relecturas == 0)
                    {
                        sqlvalues = Implode(";", valuesvi);
                        sqlvalues = "{" + std::to_string(m_idcomando) + "," + sqlvalues + "}";
                        execute_sql_ff(AmiQueries::SAVEMETERRESPONSE, sqlvalues);
                    }
                    else
                    {
                        sqlvalues = Implode(",", valuesvi);
                        sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_metercode + "," + DateTimeNow() + "," + sqlvalues + "}";
                        cout << "values: " << sqlvalues << std::endl;
                        execute_sql_ff(AmiQueries::SAVEPROFILEDATA, sqlvalues);
                        m_statusprofile = 1;

                    }
                    UpdateCommandState(5);
                }
                else
                {
                    sqlvalues = Implode(",", valuesvi);
                    sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_metercode + "," + DateTimeNow() + "," + sqlvalues + "}";
                    execute_sql_ff(AmiQueries::SAVEPROFILEDATA, sqlvalues);
                    m_statusprofile = 1;
                }
                valuesvi.clear();
                m_instantaneousvalues.clear();
                m_dlms_state = ami::DlmsState::Idle;
            }
            else
            {
                m_instantaneousvalues.clear();
                m_dlms_state = ami::DlmsState::Idle;
            }
            m_idcomando = 0;
            m_inprogress = 0;
        }
    }

}

void Session::SendProfileGeneric(std::string fechaini, std::string fechafin, int canal)
{
    //int ret;
    if (m_initialization == 0)
    {

        if ((fechaini.empty() == true) || (fechafin.empty() == true))
        {
            struct tm now = GetLocaltime();
            m_start = now;
            m_end = m_start;

            if (now.tm_hour == 0)
            {
                m_start.tm_mday = now.tm_mday - 1;
                m_start.tm_hour = 0;
                m_start.tm_min = 0;
                m_start.tm_sec = 0;
                m_end.tm_mday = m_start.tm_mday;
                m_end.tm_hour = 23;
                m_end.tm_min = 59;
                m_end.tm_sec = 0;
            }
            else
            {
                m_start.tm_hour = 0;
                m_start.tm_min = 0;
                m_start.tm_sec = 0;
                m_end.tm_min = 0;
                m_end.tm_sec = 0;

            }
        }
        else
        {
            StringToDate(fechaini, "%Y-%m-%d %H:%M:%S", m_start);
            StringToDate(fechafin, "%Y-%m-%d %H:%M:%S", m_end);
        }

        std::string strobis;
        switch (m_typeclcosem)
        {
        case 3://microstar
            strobis = "1.0.99.1." + std::to_string(canal) + ".255";
            break;
        case 0://itron
            strobis = "0.0.99.1.0.255";
            break;
        default:
            strobis = "1.0.99.1." + std::to_string(canal) + ".255";
            break;
        }
        m_objprofile = new CGXDLMSProfileGeneric(strobis);//microstar "1-0.99.1.1.255"
        m_objprofile->SetAccess(2, DLMS_ACCESS_MODE_READ);
        m_objprofile->SetSortMethod(GX_SORT_METHOD::DLMS_SORT_METHOD_FIFO);
        //CGXDLMSProfileGeneric* obj = new CGXDLMSProfileGeneric("1.0.99.1.0.255");
        if (m_typeclcosem == 40 && m_metertype == 0)
        {
            //m_Parser->SetAutoIncreaseInvokeID(true);
        }
        auto Frames = Read(m_objprofile, 3);
        for (auto& f : Frames) {
            ReadDLMSPacket(f);
        }
        m_inprogress = 1;
        UpdateCommandState(2);
        m_typemodeprofile = 1;
        /*if ((ret = Read(m_objprofile, 3)) != DLMS_ERROR_CODE_OK) //se leen primero los objetos del profile
        {
            printf("Error! Index: 3 read failed: %s\r\n", CGXDLMSConverter::GetErrorMessage(ret));
            //Continue reading.
            m_inprogress = 0;
            m_statusprofile = 0;
            UpdateCommandState(0);
            m_numperfil = 0;
        }
        else
        {
            m_inprogress = 1;
            UpdateCommandState(2);
        }
        */
        m_dlms_state = ami::DlmsState::ProfileGeneric1Resp;

    }
}

void Session::SendProfileGenericBase(std::string fechaini, std::string fechafin, int canal)
{
    //int ret;
    if (m_initialization == 0)
    {

        if ((fechaini.empty() == true) || (fechafin.empty() == true))
        {
            struct tm now = GetLocaltime();;
            m_start = now;
            m_end = m_start;
            if (now.tm_hour == 0)
            {
                m_start.tm_mday = now.tm_mday - 1;
                m_start.tm_hour = 0;
                m_start.tm_min = 0;
                m_start.tm_sec = 0;
                m_end.tm_mday = m_start.tm_mday;
                m_end.tm_hour = 23;
                m_end.tm_min = 59;
                m_end.tm_sec = 0;
            }
            else
            {
                m_start.tm_hour = 0;
                m_start.tm_min = 0;
                m_start.tm_sec = 0;
                m_end.tm_min = 0;
                m_end.tm_sec = 0;

            }
            //printf("FAuto %s - %s\n", fechaini.c_str(), fechafin.c_str());
        }
        else
        {
            StringToDate(fechaini, "%Y-%m-%d %H:%M:%S", m_start);
            StringToDate(fechafin, "%Y-%m-%d %H:%M:%S", m_end);
            //printf("Fechas %s - %s\n", fechaini.c_str(), fechafin.c_str());
        }

        std::string strobis;
        switch (canal)
        {
        case 1://standard events
        {
            //7, 0 - 0.99.98.0.255
            strobis = "0.0.99.98.0.255";
            break;
        }
        case 2://fraud events;
        {
            strobis = "0.0.99.98.1.255";
            break;
        }
        case 3://"Disconnect Control Event"
        {
            strobis = "0.0.99.98.2.255";
            break;
        }
        case 4://power quality
        {
            strobis = "0.0.99.98.4.255";
            break;
        }
        case 5://comunication event
        {
            strobis = "0.0.99.98.5.255";
            break;
        }
        case 6://power failure events
        {
            strobis = "1.0.99.97.0.255";
            break;
        }
        default:
            strobis = "";
            break;
        }
        m_objprofile = new CGXDLMSProfileGeneric(strobis);//microstar "1-0.99.1.1.255"
        m_objprofile->SetAccess(2, DLMS_ACCESS_MODE_READ);
        m_objprofile->SetSortMethod(GX_SORT_METHOD::DLMS_SORT_METHOD_FIFO);
        if (m_typeclcosem == 40 && m_metertype == 0)
        {
            //m_Parser->SetAutoIncreaseInvokeID(true);
        }
        auto Frames = Read(m_objprofile, 3);
        for (auto& f : Frames) {
            ReadDLMSPacket(f);
        }
        m_inprogress = 1;
        UpdateCommandState(2);
        /*
        if ((ret = Read(m_objprofile, 3)) != DLMS_ERROR_CODE_OK) //se leen primero los objetos del profile
        {
            printf("Error! Index: 3 read failed: %s\r\n", CGXDLMSConverter::GetErrorMessage(ret));
            m_inprogress = 0;
            m_numperfil = 0;
            //m_statusprofile = 0;
            UpdateCommandState(0);
        }
        else
        {
            m_inprogress = 1;
            UpdateCommandState(2);
        }
        */
        //m_dlmsstate = 9;//por definir *** no esta en el enum
        //m_typemodeprofile = 1;
    }
}

void Session::DisconnectControl(int opc)
{
    if (m_initialization == 0)
    {
        CGXReplyData reply;
        std::vector<CGXByteBuffer>data;
        CGXDLMSDisconnectControl dc("0.0.96.3.10.255");

        //dc.RemoteDisconnect(m_Parser, data);
        int ret;
        if (opc == 0)
        {
           // ret = dc.RemoteDisconnect(m_Parser.get(), data);
        }
        if (opc == 1)
        {
           // ret = dc.RemoteReconnect(m_Parser.get(), data);

        }
        //m_step = 6;
        if ((ret = ReadDataBlock(data)) != 0)
        {
            printf("Error!: %s\r\n", CGXDLMSConverter::GetErrorMessage(ret));
        }
        else
        {
            m_inprogress = 1;
            UpdateCommandState(2);
        }
        m_dlms_state = ami::DlmsState::DisconnectControl;
    }
}

void Session::DisconnectcontrolStatus()
{
    //int ret;
    std::string obis = "0.0.96.3.10.255";
    if (m_initialization == 0)
    {
        CGXDLMSObject* obj = new CGXDLMSObject((DLMS_OBJECT_TYPE)70, obis);
        if (obj != NULL)
        {
            auto Frames = Read(obj, 2);
            for (auto& f : Frames) {
                ReadDLMSPacket(f);
            }
            UpdateCommandState(5);
            /*
            if ((ret = Read(obj, 2)) != DLMS_ERROR_CODE_OK)
            {
                printf("Error! Index: 2 read failed: %s\r\n", CGXDLMSConverter::GetErrorMessage(ret));
                m_inprogress = 0;
                //ActualizarComando("0");
            }
            else
            {
                UpdateCommandState(5);
            }
            */
        }
        else
        {
            printf("Unknown object: %s", obis.c_str());
        }
        m_dlms_state = ami::DlmsState::DisconnectControlStatus;
    }
}

void Session::WriteProfileGeneric(std::string captureobjects, int numprofile)
{
    //int ret;
    if (m_initialization == 0)
    {
        //printf("%s : Escribiendo perfil \n", m_codigomedidor.c_str());
        std::string strobis;
        switch (m_typeclcosem)
        {
        case 3://microstar
            strobis = "1.0.99.1.1.255";
            break;
        case 0://itron
            strobis = "0.0.99.1.0.255";
            break;
        default:
            strobis = "1.0.99.1." + std::to_string(numprofile) + ".255";//el numero de profile para inh 1.0.99.1.0.255 profile 1
            break;
        }
        std::vector<std::string> obisvec;
        m_objprofile = new CGXDLMSProfileGeneric(strobis);//microstar "1-0.99.1.1.255"
        m_objprofile->SetAccess(2, DLMS_ACCESS_MODE_READ);
        m_objprofile->SetSortMethod(GX_SORT_METHOD::DLMS_SORT_METHOD_FIFO);
        Split(captureobjects, ',', &obisvec);
        if (obisvec.size() > 0)
        {
            //CGXDLMSClock* pclock = new CGXDLMSClock("0.0.1.0.0.255");
            m_objprofile->AddCaptureObject(new CGXDLMSClock("0.0.1.0.0.255"), 2, 0);
            for (size_t i = 0; i < obisvec.size(); i++)
            {
                m_objprofile->AddCaptureObject(new CGXDLMSRegister(obisvec[i]), 2, 0);
            }
            //CGXDLMSProfileGeneric* obj = new CGXDLMSProfileGeneric("1.0.99.1.0.255");
            if (m_typeclcosem == 40 && m_metertype == 0)
            {
                //m_Parser->SetAutoIncreaseInvokeID(true);
            }

            /*  funcion por convertir
            if ((ret = Write(m_objprofile, 3)) != DLMS_ERROR_CODE_OK) //se leen primero los objetos del profile
            {
                printf("Error! Index: 3 write failed: %s\r\n", CGXDLMSConverter::GetErrorMessage(ret));
                m_inprogress = 0;
                m_statusprofile = 0;
                UpdateCommandState(0);
            }
            else
            {
                m_inprogress = 1;
                UpdateCommandState(2);
            }
            */
            //m_flagrequest = 1; definir que unmero va para dlms_state
        }
        else
        {
            UpdateCommandState(-1);//falta el perfil que se va a cargar;
        }


    }
}

void Session::WritePeriodProfileGeneric(std::string captureperiod, int numprofile)
{
    //int ret;
    if (m_initialization == 0)
    {
        //printf("%s : Escribiendo periodo de perfil \n", m_codigomedidor.c_str());

        std::string strobis;
        switch (m_typeclcosem)
        {
        case 3://microstar
            strobis = "1.0.99.1.1.255";
            break;
        case 0://itron
            strobis = "0.0.99.1.0.255";
            break;
        default:
            strobis = "1.0.99.1." + std::to_string(numprofile) + ".255";//el numero de profile para inh 1.0.99.1.0.255 profile 1
            break;
        }
        m_objprofile = new CGXDLMSProfileGeneric(strobis);//microstar "1-0.99.1.1.255"
        m_objprofile->SetAccess(2, DLMS_ACCESS_MODE_READ);
        m_objprofile->SetSortMethod(GX_SORT_METHOD::DLMS_SORT_METHOD_FIFO);
        m_objprofile->SetCapturePeriod(atoi(captureperiod.c_str()));
        /*if ((ret = Write(m_objprofile, 4)) != DLMS_ERROR_CODE_OK) //se leen primero los objetos del profile
        {
            printf("Error! Index: 4 write failed: %s\r\n", CGXDLMSConverter::GetErrorMessage(ret));
            m_inprogress = 0;
            m_statusprofile = 0;
            UpdateCommandState(0);
        }
        else
        {
            m_inprogress = 1;
            UpdateCommandState(2);
            m_flagrequest = 2;
        }*/

    }
}

void Session::ReadObis(int objectype, std::string obis, int attindex, int flagrequest)
{
    //int ret;
    if (m_initialization == 0)
    {
        CGXDLMSObject* obj = new CGXDLMSObject((DLMS_OBJECT_TYPE)objectype, obis);
        if (obj != NULL)
        {
            /*
            if ((ret = Read(obj, attindex)) != DLMS_ERROR_CODE_OK)
            {
                printf("Error! Index: 2 read failed: %s\r\n", CGXDLMSConverter::GetErrorMessage(ret));
                m_inprogress = 0;
                UpdateCommandState(0);
                //Continue reading.
            }
            else
            {
                m_inprogress = 1;
                UpdateCommandState(2);
            }
            */
        }
        else
        {
            printf("Unknown object: %s", obis.c_str());
            //comm.WriteValue(GX_TRACE_LEVEL_ERROR, str);
        }
        //m_flagrequest = 7;
        //m_flagrequest = flagrequest; ***falta
    }
}

void Session::WriteObis(int objectype, std::string obis, int attindex, int flagrequest, std::string value)
{
    //int ret;
    value = "sin usar";
    if (m_initialization == 0)
    {
        CGXDLMSObject* obj = new CGXDLMSObject((DLMS_OBJECT_TYPE)objectype, obis);
        if (obj != NULL)
        {
            switch (objectype)
            {
            case DLMS_OBJECT_TYPE_CLOCK:
            {
                CGXDLMSClock* clock = new CGXDLMSClock(obis);
                clock->SetAccess(attindex, DLMS_ACCESS_MODE_READ_WRITE);
                //value es null porque la fecha la obtiene del sistema
                CGXDateTime fecha = CGXDateTime::Now();
                clock->SetTime(fecha);
                obj = clock;
                break;
            }

            default:
                break;
            }
            /*
            if ((ret = Write(obj, attindex)) != DLMS_ERROR_CODE_OK)
            {
                printf("Error! Index: 2 write failed: %s\r\n", CGXDLMSConverter::GetErrorMessage(ret));
                m_inprogress = 0;
                UpdateCommandState(0);
                //Continue reading.
            }
            else
            {
                //respuesta de lectura ok
                //printf("for : %d\n", c);
                m_inprogress = 1;
                UpdateCommandState(2);
            }
            */
        }
        else
        {
            printf("Unknown object: %s", obis.c_str());
            //comm.WriteValue(GX_TRACE_LEVEL_ERROR, str);
        }
        //m_flagrequest = 10;
        //m_flagrequest = flagrequest; ***falta
    }
}

std::vector<CGXByteBuffer> Session::Read(CGXDLMSObject* pObject, int attributeIndex)
{
    //int ret;
    m_pObject = pObject;
    m_attributeIndex = attributeIndex;
    std::vector<CGXByteBuffer> data;
    CGXReplyData reply;
    //Read data from the meter.
    int ret = m_Parser->Read(pObject, attributeIndex, data);
    if (ret == DLMS_ERROR_CODE_OK)
    {
        printf("Read OK\n");
    }
    std::string str = GXHelpers::BytesToHex(data[0].GetData(), (int)data[0].GetSize(), false);
    //printf("TX: %s\n", str.c_str());
    //m_dlmsstate = 4;
    return data;
}
boost::asio::awaitable<int> Session::GetParamsFromIP(const std::string& ip, int type)
{
    try {
		auto result = co_await query(AmiQueries::FINDDEVICEBYIP, { ip,std::to_string(type) });
		//    std::string sql = "SELECT idconcentrator::text FROM concentrators WHERE ipaddress=$1";
        if (result.countRows() > 0 && result.get(0, 0).compare("-1") != 0)
		{
			const auto& row = result.get(0);
			m_concentrator = type == 0 ? row[0] : "";
			m_metercode = type == 1 ? row[1] : "";
			m_typeclcosem = std::stoi(row[2]);
            if (type == 1)
            {
                Split(row[3].c_str(), ',', &m_OBISobjects);
                Split(row[4].c_str(), ',', &m_ScalerObjects);

            }
			printf("find DB Device  : %s\n", type == 0 ? m_concentrator.c_str() : m_metercode.c_str());
			if (on_device_identified_) {
				on_device_identified_(type == 0 ? m_concentrator : m_metercode);
			}
            m_identified = true;
			co_return 0;
		}
        else
        {
			printf("Device not found for IP %s\n", ip.c_str());
			co_return -1;
        }

        
    }
    catch (const std::exception& ex) {
        std::cerr << "Error en GetParamsFromIP_coro: " << ex.what() << std::endl;
        co_return -1;
    }
}

boost::asio::awaitable<int> Session::UpdatePerfilAsync(std::string sqlvalues)
{
    try
    {
        auto res = co_await query(AmiQueries::UPDATEPROFILE, { sqlvalues });
        if (res.countRows() > 0)
        {
            std::string dato = res.get(0, 0);
            if (!dato.empty())
            {
                m_ScalerObjects.clear();
                Split(dato, ',', &m_ScalerObjects);//actualizo esanl,dato.c_str()a; en el socket
                co_return 0;
            }

        }
        co_return -1;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error en UpdatePerfilAsync: " << ex.what() << std::endl;
        co_return -1;
    }
    
}

boost::asio::awaitable <bool> Session::SaveDataFromProfile_co()
{
    try
    {
        if (m_numperfil > 0) {
            //pido la escala del perfil
            auto res = co_await query(AmiQueries::GETPROFILEVI, { std::to_string(m_numperfil) });
            if (res.countRows() > 0)
            {
                std::string dato = res.get(0, 0);
                if (!dato.empty())
                {
                    m_ScalerObjects.clear();
                    Split(dato, ',', &m_ScalerObjects);//actualizo esanl,dato.c_str()a; en el socket
                }
                SaveDataFromProfile_Core();
            }

        }
        else {
            SaveDataFromProfile_Core();
        }

        co_return true;

    }
    catch (const std::exception& ex)
    {
        std::cerr << "error: " << ex.what() << std::endl;
        co_return false;
    }
   
}

boost::asio::awaitable<bool> Session::SavedataFromProfileAuto_co()
{
    try
    {
        if (m_numperfil > 0) {
            //pido la escala del perfil
            auto res = co_await query(AmiQueries::GETPROFILEVI, { std::to_string(m_numperfil) });
            if (res.countRows() > 0)
            {
                std::string dato = res.get(0, 0);
                if (!dato.empty())
                {
                    m_ScalerObjects.clear();
                    Split(dato, ',', &m_ScalerObjects);//actualizo esanl,dato.c_str()a; en el socket
                }
                SavedataFromProfileAuto_Core();
            }

        }
        else {
            SavedataFromProfileAuto_Core();
        }

        co_return true;

    }
    catch (const std::exception& ex)
    {
        std::cerr << "error: " << ex.what() << std::endl;
        co_return false;
    }
}
std::string Session::resolveAlias(const std::string& raw) {
    auto it = m_alias.find(raw);
    return (it != m_alias.end()) ? it->second : raw;
}
void Session::RegisterHandlers() {
    using ami::CommandType;

    m_dispatch[CommandType::InstantaneousValues]   = [](Session& s){ s.handleInstantaneousValues();   };
    m_dispatch[CommandType::ProfileGeneric]        = [](Session& s){ s.handleProfileGeneric();        };
    m_dispatch[CommandType::Reconnections]         = [](Session& s){ s.handleReconnections();         };
    m_dispatch[CommandType::ProfileChange]         = [](Session& s){ s.handleProfileChange();         };
    m_dispatch[CommandType::ProfileTimeChange]     = [](Session& s){ s.handleProfileTimeChange();     };
    m_dispatch[CommandType::CustomObisRead]        = [](Session& s){ s.handleCustomObisRead();        };
    m_dispatch[CommandType::MeterIdValidator]      = [](Session& s){ s.handleMeterIdValidator();      };
    m_dispatch[CommandType::EventProfilesByDate]   = [](Session& s){ s.handleEventProfilesByDate();   };
    m_dispatch[CommandType::CustomObisWrite]       = [](Session& s){ s.handleCustomObisWrite();       };

    m_dispatch[CommandType::RfStopReading]         = [](Session& s){ s.handleRfStopReading();         };
    m_dispatch[CommandType::RfClearMeasurement]    = [](Session& s){ s.handleRfClearMeasurement();    };
    m_dispatch[CommandType::RfClearTask]           = [](Session& s){ s.handleRfClearTask();           };
    m_dispatch[CommandType::RfF33]                 = [](Session& s){ s.handleRfF33();                 };
    m_dispatch[CommandType::RfF38]                 = [](Session& s){ s.handleRfF38();                 };
    m_dispatch[CommandType::RfF39]                 = [](Session& s){ s.handleRfF39();                 };
    m_dispatch[CommandType::RfF40]                 = [](Session& s){ s.handleRfF40();                 };
    m_dispatch[CommandType::RfReadVersion]         = [](Session& s){ s.handleRfReadVersion();         };
    m_dispatch[CommandType::RfReadNetworkParam]    = [](Session& s){ s.handleRfReadNetworkParam();    };
    m_dispatch[CommandType::RfSetNetworkParam]     = [](Session& s){ s.handleRfSetNetworkParam();     };
    m_dispatch[CommandType::RfAddNetPoint]         = [](Session& s){ s.handleRfAddNetPoint();         };
    m_dispatch[CommandType::RfSetMeasurementPoint] = [](Session& s){ s.handleRfSetMeasurementPoint(); };
    m_dispatch[CommandType::RfAddBroadcastPoint]   = [](Session& s){ s.handleRfAddBroadcastPoint();   };
    m_dispatch[CommandType::RfStartNetworking]     = [](Session& s){ s.handleRfStartNetworking();     };
    m_dispatch[CommandType::RfReadNodeLevel]       = [](Session& s){ s.handleRfReadNodeLevel();       };
    m_dispatch[CommandType::RfStopBuild]           = [](Session& s){ s.handleRfStopBuild();           };
}
// ─── case 0 ──────────────────────────────────────────────────────────
void Session::handleInstantaneousValues() {
    UpdateCommandState(1);
    SendInstantaneousValues();
}

// ─── case 1 ──────────────────────────────────────────────────────────
void Session::handleProfileGeneric() {
    int profilechanel = 0;
    int numperfil = 0;
    UpdateCommandState(1);
    std::vector<std::string> acciones;

    if (m_accion.empty()) {
        profilechanel = 0;
    }
    else {
        Split(m_accion, ',', &acciones);
        if (acciones.size() == 1) {
            profilechanel = atoi(acciones[0].c_str());
        }
        else {
            profilechanel = atoi(acciones[0].c_str());
            numperfil = atoi(acciones[1].c_str());
        }
        m_numperfil = numperfil;
    }
    SendProfileGeneric(m_fechaini, m_fechafin, profilechanel);
}

// ─── case 2 ──────────────────────────────────────────────────────────
void Session::handleReconnections() {
    UpdateCommandState(1);
    int opc = atoi(m_accion.c_str());
    DisconnectControl(opc);
}

// ─── case 5 ──────────────────────────────────────────────────────────
void Session::handleProfileChange() {
    UpdateCommandState(1);
    std::vector<std::string> acciones;
    Split(m_accion, '-', &acciones);
    if (acciones.size() != 2) {
        UpdateCommandState(-3);
    }
    else {
        WriteProfileGeneric(acciones[0], atoi(acciones[1].c_str()));
    }
}

// ─── case 6 ──────────────────────────────────────────────────────────
void Session::handleProfileTimeChange() {
    UpdateCommandState(1);
    std::vector<std::string> acciones;
    Split(m_accion, '-', &acciones);
    if (acciones.size() != 2) {
        UpdateCommandState(-3);
    }
    else {
        WritePeriodProfileGeneric(acciones[0], atoi(acciones[1].c_str()));
    }
}

// ─── case 7 ──────────────────────────────────────────────────────────
void Session::handleCustomObisRead() {
    UpdateCommandState(1);
    std::vector<std::string> acciones;
    Split(m_accion, ',', &acciones);
    if (acciones.size() != 3) {
        UpdateCommandState(-3);
    }
    else {
        ReadObis(atoi(acciones[0].c_str()), acciones[1],
            atoi(acciones[2].c_str()), 7);
    }
}

// ─── case 8 ──────────────────────────────────────────────────────────
void Session::handleMeterIdValidator() {
    UpdateCommandState(1);
    std::vector<std::string> acciones;
    Split(m_accion, ',', &acciones);
    if (acciones.size() > 0) {
        if (acciones.size() != 3) {
            UpdateCommandState(-3);
        }
        else {
            ReadObis(atoi(acciones[0].c_str()), acciones[1],
                atoi(acciones[2].c_str()), 8);
        }
    }
    else {
        ReadObis(1, "1.0.0.0.0.255", 2, 8);
    }
}

// ─── case 9 ──────────────────────────────────────────────────────────
void Session::handleEventProfilesByDate() {
    int profilechanel = 0;
    UpdateCommandState(1);
    std::vector<std::string> acciones;

    if (m_accion.empty()) {
        profilechanel = 0;
    }
    else {
        Split(m_accion, ',', &acciones);
        if (acciones.size() == 1) {
            profilechanel = atoi(acciones[0].c_str());
        }
        else {
            profilechanel = atoi(acciones[0].c_str());
        }
        m_numperfil = profilechanel;
    }
    SendProfileGenericBase(m_fechaini, m_fechafin, profilechanel);
}

// ─── case 10 ─────────────────────────────────────────────────────────
void Session::handleCustomObisWrite() {
    UpdateCommandState(1);
    std::vector<std::string> acciones, obis, values;
    Split(m_accion, '-', &acciones);
    if (acciones.size() > 1) {
        Split(acciones[0], ',', &obis);
        values.push_back(acciones[1]);          // ← FIX: era values[0] = ... sobre vector vacío
    }
    else {
        Split(acciones[0], ',', &obis);
        values.push_back("");
    }
    if (((int)acciones.size() < 0) || ((int)acciones.size() > 2)) {
        UpdateCommandState(-3);
    }
    else {
        WriteObis(atoi(obis[0].c_str()), obis[1],
            atoi(obis[2].c_str()), 3, values[0]);
    }
}

// ─── cases 100-115: RF ───────────────────────────────────────────────
void Session::handleRfStopReading() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.StopReadingTask(m_concentrator);
    std::string tmp = m_metercode + "(RF1) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfClearMeasurement() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.ClearMeasurementPoint(m_concentrator);
    std::string tmp = m_metercode + "(RF2) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfClearTask() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.ClearTask(m_concentrator);
    std::string tmp = m_metercode + "(RF3) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfF33() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.F33(m_concentrator);
    std::string tmp = m_metercode + "(RF4) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfF38() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.F38(m_concentrator);
    std::string tmp = m_metercode + "(RF5) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfF39() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.F39(m_concentrator);
    std::string tmp = m_metercode + "(RF6) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfF40() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.F40(m_concentrator);
    std::string tmp = m_metercode + "(RF7) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfReadVersion() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.ReadRfVersion(m_concentrator);
    std::string tmp = m_metercode + "(RF8) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfReadNetworkParam() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.ReadNetworkParameter(m_concentrator);
    std::string tmp = m_metercode + "(RF9) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfSetNetworkParam() {
    std::vector<std::string> acciones;
    Split(m_accion, ',', &acciones);
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.SetNetworkParameter(
        m_concentrator, atoi(acciones[0].c_str()), atoi(acciones[1].c_str()));
    std::string tmp = m_metercode + "(RF10) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfAddNetPoint() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.AddNetPoint(m_concentrator, m_accion);
    std::string tmp = m_metercode + "(RF11) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfSetMeasurementPoint() {
    std::vector<std::string> acciones;
    Split(m_accion, ',', &acciones);
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.SetMeasurementPoint(
        m_concentrator, acciones[0],
        atoi(acciones[1].c_str()), atoi(acciones[2].c_str()), atoi(acciones[3].c_str()));
    std::string tmp = m_metercode + "(RF12) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfAddBroadcastPoint() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.AddBroadcastPointNumber(m_concentrator, m_accion);
    std::string tmp = m_metercode + "(RF13) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfStartNetworking() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.StartRfNetworking(m_concentrator);
    std::string tmp = m_metercode + "(RF14) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfReadNodeLevel() {
    std::vector<std::string> acciones;
    Split(m_accion, ',', &acciones);
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.ReadNodeLevel(
        m_concentrator, atoi(acciones[0].c_str()), atoi(acciones[1].c_str()));
    std::string tmp = m_metercode + "(RF15) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}

void Session::handleRfStopBuild() {
    UpdateCommandState(1);
    std::vector<unsigned char> datos = m_gdw.StopBuild(m_concentrator);
    std::string tmp = m_metercode + "(RF16) TX:\t\t" +
        GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
    WriteLog(tmp + "\n", 3);
    m_inprogress = 1;
}