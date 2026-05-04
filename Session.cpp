#include "Session.h"
/*
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>
*/

using namespace std;
using namespace boost;
namespace asio = boost::asio;
//using boost::asio::ip::tcp;

#include <typeinfo>
#include <cxxabi.h>
#include <iostream>
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
    cout << "Sesión iniciada para " << remote_ip_ << ":" << remote_port_ << endl;
    conn_ = connector_();
    InitStart();
    //do_read(); modo callback
    auto self = shared_from_this();
    asio::co_spawn(socket_.get_executor(),
        [self]() -> asio::awaitable<void> {
            co_await self->do_read_async();
            co_return;
        },
        asio::detached);

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
    case 4059:
    {
        m_typedevice = 1;
        asio::co_spawn(socket_.get_executor(),
            [self = shared_from_this()]() -> asio::awaitable<void> {
                try
                {
                    int rc = co_await self->GetParamsFromIP_coro(self->remote_ip_, self->m_typedevice);
                    if (rc == 0) {
                        self->InitStart_AfterParams_Meters();
                    }
                    else {
                        std::cerr << "No se pudieron obtener parametros de IP ." << std::endl;
                        //solicitar codigo medidor via dlms,no se podria ya que no se sabe que tipo de cliente cosem utliza
                    }
                    co_return;
                }
                catch (const std::exception& ex)
                {
                    std::cerr << "error: " << ex.what() << std::endl;
                    co_return;
                }
            },
            asio::detached
        );
        break;
        
    }
    case 8877:
    {
        m_typedevice = 0;
        m_typesocket = 1;
        m_fabricante = 0;//dcu inhemeter
        
#if HAS_CPP20_COROUTINES    
            asio::co_spawn(socket_.get_executor(),
                [self = shared_from_this()]() -> asio::awaitable<void> {
                    try
                    {
                        int rc = co_await self->GetParamsFromIP_coro(self->remote_ip_, self->m_typedevice);
                        if (rc == 0) {
                            self->m_typeclcosem = 6;
                            self->start_timer_dcu();
                        }
                        else {
                            std::cerr << "No se pudieron obtener parametros de IP ." << std::endl;
                            //solicitar codigo medidor via dlms,no se podria ya que no se sabe que tipo de cliente cosem utliza
                        }
                        co_return;
                    }
                    catch (const std::exception& ex)
                    {
                        std::cerr << "error: " << ex.what() << std::endl;
                        co_return;
                    }
                },
                asio::detached
            );
#endif
        
        break;
    }
    }
}

void Session::InitStart_AfterParams_Meters()
{
    InitClientCosem();
    if ((!m_authenticationkey.empty() && !m_cipherkey.empty()) && remote_port_ == m_portmacrostar)
    {
        //InitializeConnection(1);
        m_updateframecounter = 1;
        m_inprogress = 1;//revisar si se lo activa
        std::string sqlvalues = "{" + m_codigomedidor + "," + "InitializeConnectionFC,0}";
        //basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqlvalues });
        execute_sql("select * from dlms.monitormedidor($1::varchar[])", sqlvalues);
        //self->UpdateFrameCounter();
    }
    else {
        SnrmRequest();

    }
}

void Session::InitVars()
{
	m_comands["C01"] = 0;//instantaneous values
    m_comands["C02"] = 1;//profile by date
    m_comands["C03"] = 2;//power off - on
    m_comands["C04"] = 3;//limite de potencia
	m_comands["C05"] = 4;//tariff change
	m_comands["C06"] = 5;//profile change
    m_comands["C07"] = 6;//profile time change
    m_comands["X01"] = 7;//custom command obis (read)
    m_comands["C08"] = 8;//meter id validator
    m_comands["C09"] = 9;//event profiles by date
    m_comands["X02"] = 10;//custom command obis (write)
    m_comands["C10"] = 11;//profile by range 
    m_comands["C11"] = 12;//event profile by range
	m_comands["C12"] = 49;//close connection
    m_idcomando = 0;

}

void Session::do_read() {
    auto self(shared_from_this());
    socket_.async_read_some(asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec/*std::error_code ec*/, std::size_t length) {
            if (!ec) {
                //std::string message(data_, length);
                //process_message(message);
                std::vector<uint8_t> raw(data_, data_ + length);
                onRawData(raw);
                do_read();
            }
            else if (ec == asio::error::eof || ec == asio::error::connection_reset || ec == asio::error::operation_aborted)
            {
                cout << "Dispositivo desconectado " << remote_ip_ << ":" << remote_port_ << endl;
                Close();
            }
            else
            {
                cerr << "error en lectura " << ec.message() << std::endl;
                Close();
            }
        });
}

asio::awaitable<void> Session::do_read_async()
{
    try {
        while (true) {
            std::size_t length = co_await socket_.async_read_some(asio::buffer(data_, max_length), asio::use_awaitable);
            std::vector<uint8_t> raw(data_, data_ + length);
            co_await onRawDataAsync(raw); // también debe ser corutina
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "Error en lectura: " << ex.what() << std::endl;
        Close();
    }

}

void Session::do_write(const std::string& message) {
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(message),
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

void Session::onRawData(const std::vector<uint8_t>& raw)
{
    CGXByteBuffer buf;
    std::string respuesta = ByteToString((unsigned char *)raw.data(), 0, (int)raw.size());// = GXHelpers::BytesToHex(buf.GetData(), (int)buf.GetSize(), false);
    std::string fecha;
    std::vector<CGXByteBuffer> data;//variable sirve para dcu y meter
    CGXByteBuffer bb, bb2, databuffer;
    CGXReplyData notify;

    switch (m_typedevice)
    {
    case 0: //dcu inhemeter
    {
        CGXByteBuffer bb, bb2, databuffer;
        CGXReplyData notify;
        Now2(fecha);
        respuesta = "RX:" + fecha + " " + m_concentrator + "|" + m_codigomedidor + "[" + std::to_string(m_dlmsstate) + "] " + respuesta;
        printf("%s\n", respuesta.c_str());
        //agregado log semanal
        WriteLog(respuesta + "\n", 3);
        if (raw[0] == 0x0D)
        {
            return;
            /*
            tiempo = time(NULL);//opcional para que lo tome como pulso
            if (raw.size() == 13) //pulso inhemeter
            {

                if (m_codigomedidor.empty())
                {
                    m_codigomedidor = GetMeterInhfromPulse((unsigned char*)buf);
                    std::string sqla = "{" + m_codigomedidor + "," + "NewMeter,0," + m_direccionip + "}";
                    basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqla });
                }
                char* vectmp = (char*)buf;
                vectmp[0] = (char)0x0c;
                vectmp[9] = (char)0x80;
                tiempo = time(NULL);//actua como pulso, para actualizar el tiempo del downtime
                std::string sqlvalues = "{" + m_codigomedidor + "," + "HeartBeat,0}";
                basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqlvalues });
                SendBuf(vectmp, 12);

            }
            */
        }
        //Valida si el frame que llega es dlms
        std::vector<unsigned char> buffvector = m_gdw.DecodeFrame(raw.data(), (int)raw.size());
        printf("RX1:%s\n", ByteToString(buffvector.data(), 0, (int)buffvector.size()).c_str());
        buf.Set(buffvector.data(), buffvector.size());
        int lendlms = (int)buffvector.size();
        if (lendlms == 0)
        {
            printf("%s entro lendlms0\n", m_concentrator.c_str());
            if (m_gdw.GetSubframeType() == 0x01 && (m_idcomando > 0))
            {
                UpdateCommandState(-9);//offline
                m_idcomando = 0;
                m_inprogress = 0;
                m_numperfil = 0;
                return;
            }
            else if (m_gdw.GetSubframeType() == 0x00 && (m_idcomando > 0)) //lo genera un error por parte de macrodcu
            {
                UpdateCommandState(-1);
                m_idcomando = 0;
                m_inprogress = 0;
                m_numperfil = 0;
                return;
            }
            else
            {
                printf("%s no detecto trama\n", m_concentrator.c_str());
                return;
            }
        }

        int opc = m_gdw.Getframetype();
        if (opc == 1) //hearbeat
        {
            //asi determina que mientras se este ejecutando un comando el timer de seguimiento corra por otro lado
            if (m_inprogress == 0)
            {
                tiempo = time(nullptr);
            }
            if (m_concentrator.empty())
            {
                m_concentrator = m_gdw.GetConcentrator();
                printf("Concentrador detectado %s\n", m_concentrator.c_str());
                if (!m_concentrator.empty())
                {
                    //***experimental para dcu que comparten ip
                    m_typeclcosem = 6;//hdlc para todos
                    if (on_device_identified_)
                    {
                        on_device_identified_(m_concentrator);
                    }
                    start_timer_dcu();

                    //***FALTA agregar en proceidmiento**
                    //std::string sqla = "{" + m_concentrator + "," + "NewDCU,0," + m_direccionip + "}";
                    //basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqla });
                }
            }
            std::string sqlvalues = "{" + m_concentrator + "," + "HeartBeat, 0," + std::to_string(remote_port_) + " }";
            //basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqlvalues });
            execute_sql("select * from dlms.monitormedidor($1::varchar[])", sqlvalues);
            fecha.clear();
            Now2(fecha);
            respuesta = "TX:" + fecha + " " + m_concentrator + "|" + m_codigomedidor + "[" + std::to_string(m_dlmsstate) + "] " + ByteToString(buffvector.data(), 0, (int)buffvector.size()).c_str();
            printf("%s\n", respuesta.c_str());
            WriteLog(respuesta + "\n", 3);
            SendBuffer(buffvector);
            return;
        }
        if (opc == 3)//rf events
        {
            //asi determina que mientras se este ejecutando un comando el timer de seguimiento corra por otro lado
            if (m_inprogress == 0)
            {
                tiempo = time(NULL);
            }
            std::string rawevent = m_gdw.ParseEvents(buffvector);
            std::string sqlvalues = "{" + m_concentrator + "," + rawevent + "}";
            //basedatos.SConsulta("select* from dlms.guardareventodcuinh($1)", { sqlvalues });
            execute_sql("select * from dlms.guardareventodcuinh($1::varchar[])", sqlvalues);
            return;
        }
        //CGXDLMSObject* pObject;
        //int attributeIndex;
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
                    return;
                }
                if (!notify.IsMoreData())
                {
                    //ParseReplyNotification(notify);
                    notify.Clear();
                    return;
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
            }*/
        }
        CGXByteBuffer tmp2 = m_reply.GetData();
        std::cout <<m_dlmsstate <<"-reply0: " << ByteToString(tmp2.GetData(), 0, (int)tmp2.GetSize()) << std::endl;
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
            if ((ret = ReadDLMSPacket(bb2)) != DLMS_ERROR_CODE_OK)
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
    
    case 1://meter inhemeter por desarrollar
    {
        buf.Set(raw.data(), raw.size());
        /*if (self->on_device_identified_)
        {
            self->on_device_identified_(self->m_concentrator);
        }*/
        break;
    }

    }

    if (m_loop == 1)
    {
        return;
    }

    switch (m_dlmsstate)
    {
    case 1://snmr response
    {
        AARQRequest(m_reply.GetData());
        break;
    }
    case 2:
    {
        AAREResponse(m_reply.GetData());
        break;
    }
    case 3:
    {
        int ret = m_Parser->ParseApplicationAssociationResponse(m_reply.GetData());
        if (ret != 0)
        {
            printf("Authentication failed (%d) %s\n", ret, CGXDLMSConverter::GetErrorMessage(ret));
            std::string sqlvalues = "{" + m_codigomedidor + "," + "Authentication Failed,0}";
            execute_sql("select * from dlms.monitormedidor($1::varchar[])", sqlvalues);
            //return ret;
            m_initialization = -1;
        }
        else
        {
            m_initialization = 0;//ok
            m_inprogress = 0;
            //printf("%s : Initialization ok\n", m_codigomedidor.c_str());
            std::string sqlvalues = "{" + m_codigomedidor + "," + "Initialization Ok,0}";
            execute_sql("select * from dlms.monitormedidor($1::varchar[])", sqlvalues);
            /*execute_query_async("select * from dlms.monitormedidor($1::varchar[])", [this](ozo::error_code ec) {
                if (ec) {
                    std::cerr << "Error en execute_query_async: " << ec.message() << std::endl;
                }
                else {
                    std::cout << "Consulta de monitor ejecutada exitosamente." << std::endl;
                }
                }, sqlvalues);
            */
            if (m_typecommand == 9)
            {
                GetIdMeter();
                m_typecommand = -1;
            }
        }
        break;
    }
    case 8://disconnect response
    {
        //reply.Clear();
        //std::string dcucl = std::to_string(m_conhes) + "01";//revisar***
        int cli = GetServerAddress(m_conhes, 0x01);
        SetClientCosem(cli, m_authenticationkey, m_cipherkey, m_dedicatekey);
        SnrmRequest();
        m_dlmsstate = 1;
        break;
    }
    case 11://instantaneous values response
    {
        ResponseInstantaneousValues();
        break;
    }
    case 20://profilegeneric 1 response
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
            m_dlmsstate = 0;
            m_idcomando = 0;
            m_inprogress = 0;
            //reseteo variables
            m_numperfil = 0;
            //*** Resetear la conexion para medidores star
            if (m_typesocket == 0)
            {
                UpdateDBMeterEstatus(0);
            }
            //SetCloseAndDelete(); anteriomente cerraba socket aqui se podria?

            return;
        }
        //printf("profile generic 1 \n");
        if (m_valuefromobject.substr(0, 1) == "[")
        {
            std::string objectsprofile;
            objectsprofile = m_valuefromobject.substr(1, m_valuefromobject.size() - 2);
            std::vector<std::string> arrayobjects;
            Explode(objectsprofile, ",", &arrayobjects);
            m_arrayobjectsobis.clear();
            m_arrayobjectsobis = arrayobjects;
            //printf("profile generic 1-1 \n");
            if (arrayobjects.size() < 2)
            {
                //printf("%s perfil 1 FAIL no tiene perfil cargado \n ", m_codigomedidor.c_str());
                UpdateCommandState(-5);
                m_dlmsstate = 0;
                m_idcomando = 0;
                m_inprogress = 0;
                //reseteo variables
                m_numperfil = 0;
                return;
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
                printf("%s perfil Diferente \n ", m_codigomedidor.c_str());
                UpdateCommandState(-2);
                std::string comando = std::to_string(m_idcomando);
                m_dlmsstate = 0;
                m_idcomando = 0;
                m_inprogress = 0;
                m_numperfil = 0;
                //agregado cambio de perfil en linea si existe en sistema
                std::string obiscompare = Implode(",", arrayobjectscompare);

                std::string sqlvalues = "{" + std::to_string(m_metertype) + "," + comando + "," + m_codigomedidor + "," + "\"" + obiscompare + "\"" + "}";
                printf("modificando perfil %s\n", sqlvalues.c_str());
                
                boost::asio::co_spawn(socket_.get_executor(),
                    [self = shared_from_this(), sqlvalues, arrayobjectscompare]() -> asio::awaitable<void> {
                        try
                        {
                            auto res = co_await self->conn_->execute_query("select * from dlms.CambiarPerfilxError($1)", { sqlvalues });
                            if (res.countRows() > 0) {
                                std::string dato = res.get(0, 0);
                                Explode(dato, ",", &self->m_ScalerObjects);
                                self->m_OBISobjects = arrayobjectscompare;
                            }
                            co_return;

                        }
                        catch (const std::exception& ex)
                        {
                            std::cerr << "error: " << ex.what() << std::endl;
                            co_return;
                        }
                    },
                    asio::detached);
                    
                return;
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
        m_dlmsstate = 21;
        UpdateCommandState(3);
        //if ((ret = m_Parser->ReadRowsByRange(pObject, start, end, data)) != 0 ||
        //    (ret = ReadDataBlock(data, reply)) != 0 ||
        //    (ret = m_Parser->UpdateValue(*pObject, 2, reply.GetValue())) != 0)
        //{
        //    return ret;
        //}

        break;

    }
    case 21://profile generic 2 response
    {
        int ret = m_Parser->UpdateValue(*m_objprofile, 2, m_reply.GetValue());
        if (ret != 0)
        {
            printf(" %s error UpdateValue: %s\n", m_codigomedidor.c_str(), CGXDLMSConverter::GetErrorMessage(ret));
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
            boost::asio::co_spawn(socket_.get_executor(), [self = shared_from_this()]() -> asio::awaitable<void> { 
                try {
                    bool ok= co_await self->SaveDataFromProfile_co();
                    if (ok) {
                        self->m_dlmsstate = 0;
                        self->m_idcomando = 0;
                        self->m_inprogress = 0;
                        self->m_numperfil = 0;
                    }
                }
                catch (const std::exception& ex) {
                    std::cerr << "error en SaveDataFromProfile_co " << ex.what() << std::endl;
                 }
                }, asio::detached);
            //SaveDataFromProfile();
        }
        else //AUTOMATICO
        {
            //boost::asio::co_spawn(socket_.get_executor(), [self = shared_from_this()]() -> asio::awaitable<void> { co_await self->SavedataFromProfileAuto(); }, asio::detached);
            //CGXDateTime df;
            
            m_dlmsstate = 0;
            m_idcomando = 0;
            m_inprogress = 0;
            m_numperfil = 0;
        }
        break;
    }
    case 101://invocationcounter response
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

        m_dlmsstate = 0;
        m_initialization = -1;
        Disconnect();

        break;
    }
    case 99:// getmeterid
    {
        break;
    }

    }
}

asio::awaitable<void> Session::onRawDataAsync(const std::vector<uint8_t>& raw)
{
    CGXByteBuffer buf;
    std::string respuesta = ByteToString((unsigned char*)raw.data(), 0, (int)raw.size());// = GXHelpers::BytesToHex(buf.GetData(), (int)buf.GetSize(), false);
    std::string fecha;
    std::vector<CGXByteBuffer> data;//variable sirve para dcu y meter
    CGXByteBuffer bb, bb2, databuffer;
    CGXReplyData notify;

    switch (m_typedevice)
    {
    case 0: //dcu inhemeter
    {
        CGXByteBuffer bb, bb2, databuffer;
        CGXReplyData notify;
        Now2(fecha);
        respuesta = "RX:" + fecha + " " + m_concentrator + "|" + m_codigomedidor + "[" + std::to_string(m_dlmsstate) + "] " + respuesta;
        printf("%s\n", respuesta.c_str());
        //agregado log semanal
        WriteLog(respuesta + "\n", 3);
        if (raw[0] == 0x0D)
        {
            co_return;
            /*
            tiempo = time(NULL);//opcional para que lo tome como pulso
            if (raw.size() == 13) //pulso inhemeter
            {

                if (m_codigomedidor.empty())
                {
                    m_codigomedidor = GetMeterInhfromPulse((unsigned char*)buf);
                    std::string sqla = "{" + m_codigomedidor + "," + "NewMeter,0," + m_direccionip + "}";
                    basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqla });
                }
                char* vectmp = (char*)buf;
                vectmp[0] = (char)0x0c;
                vectmp[9] = (char)0x80;
                tiempo = time(NULL);//actua como pulso, para actualizar el tiempo del downtime
                std::string sqlvalues = "{" + m_codigomedidor + "," + "HeartBeat,0}";
                basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqlvalues });
                SendBuf(vectmp, 12);

            }
            */
        }
        //Valida si el frame que llega es dlms
        std::vector<unsigned char> buffvector = m_gdw.DecodeFrame(raw.data(), (int)raw.size());
        printf("RX1:%s\n", ByteToString(buffvector.data(), 0, (int)buffvector.size()).c_str());
        buf.Set(buffvector.data(), buffvector.size());
        int lendlms = (int)buffvector.size();
        if (lendlms == 0)
        {
            printf("%s entro lendlms0\n", m_concentrator.c_str());
            if (m_gdw.GetSubframeType() == 0x01 && (m_idcomando > 0))
            {
                UpdateCommandState(-9);//offline
                m_idcomando = 0;
                m_inprogress = 0;
                m_numperfil = 0;
                co_return;
            }
            else if (m_gdw.GetSubframeType() == 0x00 && (m_idcomando > 0)) //lo genera un error por parte de macrodcu
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

        int opc = m_gdw.Getframetype();
        if (opc == 1) //hearbeat
        {
            //asi determina que mientras se este ejecutando un comando el timer de seguimiento corra por otro lado
            if (m_inprogress == 0)
            {
                tiempo = time(nullptr);
            }
            if (m_concentrator.empty())
            {
                m_concentrator = m_gdw.GetConcentrator();
                printf("Concentrador detectado %s\n", m_concentrator.c_str());
                if (!m_concentrator.empty())
                {
                    //***experimental para dcu que comparten ip
                    m_typeclcosem = 6;//hdlc para todos
                    if (on_device_identified_)
                    {
                        on_device_identified_(m_concentrator);
                    }
                    start_timer_dcu();

                    //***FALTA agregar en proceidmiento**
                    //std::string sqla = "{" + m_concentrator + "," + "NewDCU,0," + m_direccionip + "}";
                    //basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqla });
                }
            }
            std::string sqlvalues = "{" + m_concentrator + "," + "HeartBeat, 0," + std::to_string(remote_port_) + " }";
            //basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqlvalues });
            co_await conn_->execute_non_query("select * from dlms.monitormedidor($1::varchar[])", { sqlvalues });
            //execute_sql("select * from dlms.monitormedidor($1::varchar[])", sqlvalues);
            fecha.clear();
            Now2(fecha);
            respuesta = "TX:" + fecha + " " + m_concentrator + "|" + m_codigomedidor + "[" + std::to_string(m_dlmsstate) + "] " + ByteToString(buffvector.data(), 0, (int)buffvector.size()).c_str();
            printf("%s\n", respuesta.c_str());
            WriteLog(respuesta + "\n", 3);
            co_await SendDataAsync(buffvector);
            co_return;
        }
        if (opc == 3)//rf events
        {
            //asi determina que mientras se este ejecutando un comando el timer de seguimiento corra por otro lado
            if (m_inprogress == 0)
            {
                tiempo = time(NULL);
            }
            std::string rawevent = m_gdw.ParseEvents(buffvector);
            std::string sqlvalues = "{" + m_concentrator + "," + rawevent + "}";
            //basedatos.SConsulta("select* from dlms.guardareventodcuinh($1)", { sqlvalues });
            co_await conn_->execute_non_query("select * from dlms.guardareventodcuinh($1::varchar[])", { sqlvalues });
            //execute_sql("select * from dlms.guardareventodcuinh($1::varchar[])", sqlvalues);
            co_return;
        }
        //CGXDLMSObject* pObject;
        //int attributeIndex;
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
            }*/
        }
        CGXByteBuffer tmp2 = m_reply.GetData();
        std::cout << m_dlmsstate << "-reply0: " << ByteToString(tmp2.GetData(), 0, (int)tmp2.GetSize()) << std::endl;
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

    case 1://meter inhemeter por desarrollar
    {
        buf.Set(raw.data(), raw.size());
        /*if (self->on_device_identified_)
        {
            self->on_device_identified_(self->m_concentrator);
        }*/
        break;
    }

    }

    if (m_loop == 1)
    {
        co_return;
    }

    switch (m_dlmsstate)
    {
    case 1://snmr response
    {
        AARQRequest(m_reply.GetData());
        break;
    }
    case 2:
    {
        AAREResponse(m_reply.GetData());
        break;
    }
    case 3:
    {
        int ret = m_Parser->ParseApplicationAssociationResponse(m_reply.GetData());
        if (ret != 0)
        {
            printf("Authentication failed (%d) %s\n", ret, CGXDLMSConverter::GetErrorMessage(ret));
            std::string sqlvalues = "{" + m_codigomedidor + "," + "Authentication Failed,0}";
            //execute_sql("select * from dlms.monitormedidor($1::varchar[])", sqlvalues);
            co_await conn_->execute_non_query("select * from dlms.monitormedidor($1::varchar[])", { sqlvalues });
            //return ret;
            m_initialization = -1;
        }
        else
        {
            m_initialization = 0;//ok
            m_inprogress = 0;
            //printf("%s : Initialization ok\n", m_codigomedidor.c_str());
            std::string sqlvalues = "{" + m_codigomedidor + "," + "Initialization Ok,0}";
            //execute_sql("select * from dlms.monitormedidor($1::varchar[])", sqlvalues);
            co_await conn_->execute_non_query("select * from dlms.monitormedidor($1::varchar[])", { sqlvalues });
            if (m_typecommand == 9)
            {
                GetIdMeter();
                m_typecommand = -1;
            }
        }
        break;
    }
    case 8://disconnect response
    {
        //reply.Clear();
        //std::string dcucl = std::to_string(m_conhes) + "01";//revisar***
        int cli = GetServerAddress(m_conhes, 0x01);
        SetClientCosem(cli, m_authenticationkey, m_cipherkey, m_dedicatekey);
        SnrmRequest();
        m_dlmsstate = 1;
        break;
    }
    case 11://instantaneous values response
    {
        ResponseInstantaneousValues();
        break;
    }
    case 20://profilegeneric 1 response
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
            m_dlmsstate = 0;
            m_idcomando = 0;
            m_inprogress = 0;
            //reseteo variables
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
            Explode(objectsprofile, ",", &arrayobjects);
            m_arrayobjectsobis.clear();
            m_arrayobjectsobis = arrayobjects;
            //printf("profile generic 1-1 \n");
            if (arrayobjects.size() < 2)
            {
                //printf("%s perfil 1 FAIL no tiene perfil cargado \n ", m_codigomedidor.c_str());
                UpdateCommandState(-5);
                m_dlmsstate = 0;
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
                printf("%s perfil Diferente \n ", m_codigomedidor.c_str());
                UpdateCommandState(-2);
                std::string comando = std::to_string(m_idcomando);
                m_dlmsstate = 0;
                m_idcomando = 0;
                m_inprogress = 0;
                m_numperfil = 0;
                //agregado cambio de perfil en linea si existe en sistema
                std::string obiscompare = Implode(",", arrayobjectscompare);

                std::string sqlvalues = "{" + std::to_string(m_metertype) + "," + comando + "," + m_codigomedidor + "," + "\"" + obiscompare + "\"" + "}";
                printf("modificando perfil %s\n", sqlvalues.c_str());

                auto res = co_await conn_->execute_query("select * from dlms.CambiarPerfilxError($1)", { sqlvalues });
                if (res.countRows() > 0) {
                    std::string dato = res.get(0, 0);
                    Explode(dato, ",", &m_ScalerObjects);
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
        m_dlmsstate = 21;
        UpdateCommandState(3);
        //if ((ret = m_Parser->ReadRowsByRange(pObject, start, end, data)) != 0 ||
        //    (ret = ReadDataBlock(data, reply)) != 0 ||
        //    (ret = m_Parser->UpdateValue(*pObject, 2, reply.GetValue())) != 0)
        //{
        //    return ret;
        //}

        break;

    }
    case 21://profile generic 2 response
    {
        int ret = m_Parser->UpdateValue(*m_objprofile, 2, m_reply.GetValue());
        if (ret != 0)
        {
            printf(" %s error UpdateValue: %s\n", m_codigomedidor.c_str(), CGXDLMSConverter::GetErrorMessage(ret));
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
                    m_dlmsstate = 0;
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
                    m_dlmsstate = 0;
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
    case 101://invocationcounter response
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

        m_dlmsstate = 0;
        m_initialization = -1;
        Disconnect();

        break;
    }
    case 99:// getmeterid
    {
        break;
    }

    }
}

int Session::SnrmRequest()
{
    std::string sqlvalues = "{" + m_codigomedidor + "," + "InitializeConnection-SNMR,0}";
    //basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqlvalues });
    execute_sql("select * from dlms.monitormedidor($1::varchar[])", sqlvalues);
    std::vector<CGXByteBuffer> data;
    //CGXReplyData reply;
    int ret = m_Parser->SNRMRequest(data);
    if (ret != 0) {
        std::cerr << "[" << m_codigomedidor << "] Error creando SNRM\n";
    }
    for (auto& f : data) {
        ReadDLMSPacket(f);
    }
    m_dlmsstate = 1;
    return DLMS_ERROR_CODE_OK;
}

int Session::AARQRequest(CGXByteBuffer& uaResponse)
{
    std::vector<CGXByteBuffer> data;
    //CGXReplyData reply;
    //reply.SetData(uaResponse);

    int ret = m_Parser->ParseUAResponse(uaResponse/*m_reply.GetData()*/);
    if (ret != 0) {
        std::cerr << "[" << m_codigomedidor << "] Error parseando UA\n";
        return DLMS_ERROR_CODE_INVALID_RESPONSE;
    }
    ret = m_Parser->AARQRequest(data);
    if (ret != 0) {
        std::cerr << "[" << m_codigomedidor << "] Error creando AARQ\n";
        return DLMS_ERROR_CODE_FALSE;
    }
    ReadDataBlock(data);
  
    m_dlmsstate = 2;
    //std::cout << "AARQRequest OK" << std::endl;
    return DLMS_ERROR_CODE_OK;
}

int Session::AAREResponse(CGXByteBuffer& uaResponse)
{
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
                m_dlmsstate = 3;
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
            m_dlmsstate = 0;//varaible clave
            m_inprogress = 0;
            std::string sqlvalues = "{" + m_codigomedidor + "," + "Initialization OK,0}";
            //basedatos.SConsulta("select * from dlms.monitormedidor($1)", { sqlvalues });
            execute_sql("select * from dlms.monitormedidor($1::varchar[])", sqlvalues);
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
                if (m_typecommand == 9)
                {
                    GetIdMeter();
                    m_typecommand = -1;
                }
                break;
            }
            }
            
        }
    }
    return 0;
}

int Session::UpdateFrameCounter()
{
    int ret = 0;
    //Read frame counter if GeneralProtection is used.
    if (m_updateframecounter == 1)
        //if (m_Parser->GetCiphering() != NULL && m_Parser->GetCiphering()->GetSecurity() != DLMS_SECURITY_NONE)
    {
        //printf("UpdateFrameCounter\n");
        m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_ACTION));
        ///lo lee desde initclientcosem
        //m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_ACTION));
        //m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(m_Parser->GetProposedConformance() | DLMS_CONFORMANCE_GENERAL_PROTECTION));
        std::vector<CGXByteBuffer> data;
        CGXReplyData reply;
        int cli = GetServerAddress(m_conhes, 0x10);
        m_Parser->SetClientAddress((unsigned long)cli); //cli,server=>0xzz10,0x0001
        m_Parser->SetAuthentication(DLMS_AUTHENTICATION_NONE);
        m_Parser->GetCiphering()->SetSecurity(DLMS_SECURITY_NONE);
        //Get meter's send and receive buffers size.
        ret = m_Parser->SNRMRequest(data);
        ret = ReadDataBlock(data); //aqui no hay problema que data este vacio porque internamente valida y retorna 0 y no envia para que no se active el do_read
        if (ret == 0 && data.size() == 0)
        {
            ret = m_Parser->ParseUAResponse(reply.GetData());
            if (ret == 0)
            {
                reply.Clear();
                ret = m_Parser->AARQRequest(data);
                ret = ReadDataBlock(data);
                m_dlmsstate = 2;
            }
        }
        else
        {
            m_dlmsstate = 1;
        }

        reply.Clear();
    }
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
        std::vector<unsigned char> datos = m_gdw.PacketFrame2(dlms, m_concentrator, m_codigomedidor);
        CGXByteBuffer dtemp;
        dtemp.Set(datos.data(), datos.size());
        std::string respuesta, fecha;
        Now2(fecha);
        respuesta = "TX:" + fecha + " " + m_concentrator + "|" + m_codigomedidor + "[" + std::to_string(m_dlmsstate) + "] " + ByteToString(datos.data(), 0, (int)datos.size()).c_str();
        printf("%s\n", respuesta.c_str());
        WriteLog(respuesta + "\n", 3);
        SendData(dtemp);

    }
    else
    {
        std::cout << "[" << m_codigomedidor << "] TX Frame: " << data.ToHexString() << "\n";
        SendData(data);

    }
    
    return 0;
}

asio::awaitable<int> Session::ReadDLMSPacketAsync(CGXByteBuffer data)
{
    if (data.GetSize() == 0) co_return 0;

    if (remote_port_ == 8877)
    {
        std::string dlms = ByteToString(data.GetData(), 0, (int)data.GetSize());
        if (dlms.empty()) {
            printf("dlms vacio \n");
        }
        else {
            printf("TX1:%s\n", dlms.c_str());
        }

        std::vector<unsigned char> datos = m_gdw.PacketFrame2(dlms, m_concentrator, m_codigomedidor);
        CGXByteBuffer dtemp;
        dtemp.Set(datos.data(), datos.size());

        std::string respuesta, fecha;
        Now2(fecha);
        respuesta = "TX:" + fecha + " " + m_concentrator + "|" + m_codigomedidor + "[" + std::to_string(m_dlmsstate) + "] " + ByteToString(datos.data(), 0, (int)datos.size()).c_str();
        printf("%s\n", respuesta.c_str());
        WriteLog(respuesta + "\n", 3);

        int result = co_await SendDataAsync(dtemp);
        co_return result;
    }
    else
    {
        std::cout << "[" << m_codigomedidor << "] TX Frame: " << data.ToHexString() << "\n";
        int result = co_await SendDataAsync(data);
        co_return result;
    }

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
    asio::co_spawn(socket_.get_executor(),
        [self, data = std::move(data)]() -> asio::awaitable<void> {
            int result = co_await self->SendDataAsync(std::move(data));
            // Puedes agregar lógica adicional según el resultado si lo deseas
            co_return;
        },
        asio::detached);

}

asio::awaitable<int> Session::SendDataAsync(CGXByteBuffer data)
{
    try {
        co_await asio::async_write(socket_, asio::buffer(data.GetData(), data.GetSize()), asio::use_awaitable);
        //std::cout << "[" << m_codigomedidor << "]\n"; // << data.ToHexString() << "\n";
        co_return 0; // éxito
    }
    catch (const std::exception& ex) {
        std::cout << "Error enviando a " << m_codigomedidor << ": " << ex.what() << "\n";
        co_return -1; // error
    }
}

asio::awaitable<int> Session::SendDataAsync(const std::vector<unsigned char>& data)
{
    try {
        co_await asio::async_write(socket_, asio::buffer(data.data(), data.size()), asio::use_awaitable);
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

asio::awaitable<int> Session::ReadDataBlockAsync(std::vector<CGXByteBuffer>& data)
{
    if (data.empty()) co_return 0;

    m_dataBlock.assign(data.begin(), data.end());
    m_indexDataBlock = 0;

    int result = co_await ReadDLMSPacketAsync(m_dataBlock[m_indexDataBlock]);
    co_return result;
}

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
        if (m_passwordmeter == "")
        {
            //m_Parser = new CGXDLMSSecureClient(true, 16, 1);
            m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 16, 1);

        }
        else
        {
            //m_Parser = new CGXDLMSSecureClient(true, 0x20, 1, DLMS_AUTHENTICATION_LOW, m_passwordmeter.c_str());
            m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 0x20, 1, DLMS_AUTHENTICATION_LOW, m_passwordmeter.c_str());

        }

        m_Parser->SetDateTimeSkips((DATETIME_SKIPS)(DATETIME_SKIPS_DEVITATION | DATETIME_SKIPS_STATUS));
        m_metertype = 1;
        break;
    }
    case 4://inhemeter
    {
        m_typeinterface = 1;
        std::string password = "INHE-0410-WRITE";
        //m_Parser = new CGXDLMSSecureClient(true, 1, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, "INHE-0410-WRITE", DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 1, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, password.c_str(), DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
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
        std::string password = "12345678";
        //m_Parser = new CGXDLMSSecureClient(true, 18, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, "12345678", DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 18, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, password.c_str(), DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
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
        //m_Parser = new CGXDLMSSecureClient(true, 1, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, "INHE-0410-WRITE", DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_HDLC);
        std::string password = "INHE-0410-WRITE";
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 1, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, password.c_str(), DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_HDLC);
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
    case 7://macromedidores star necesitan llaves
    {
        m_typeinterface = 1;
        //m_Parser = new CGXDLMSSecureClient(true, 1, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_NONE, NULL, DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 1, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_NONE, nullptr, DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser->SetProposedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_ACCESS));
        m_Parser->SetServiceClass(DLMS_SERVICE_CLASS::DLMS_SERVICE_CLASS_CONFIRMED);
        m_Parser->SetAutoIncreaseInvokeID(true);
        m_metertype = 0;
        break;
    }
    case 8://medidores star, por verificar
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
        std::string password = "12345678";
        //m_Parser = new CGXDLMSSecureClient(true, 18, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, "12345678", DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
        m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 18, 1, DLMS_AUTHENTICATION::DLMS_AUTHENTICATION_LOW, password.c_str(), DLMS_INTERFACE_TYPE::DLMS_INTERFACE_TYPE_WRAPPER);
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

void Session::InitClientCosemDynamic(const std::unordered_map<int, MeterConfig>& configs, int type)
{
    return;
}

asio::awaitable<void> Session::WriteLogAsync(const std::string& stringlog, int server)
{
    co_await asio::post(asio::system_executor(), asio::use_awaitable);
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
                        sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_codigomedidor + "," + res + "}";
                        sqlvalues_batch += sqlvalues + ",";
                        //basedatos.SConsulta("select * from dlms.guardarlecturamedidor($1)", { sqlvalues });
                        //execute_sql("select * from dlms.guardarlecturamedidor($1::varchar[])", sqlvalues);
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
                    //basedatos.SConsulta("select * from dlms.guardarrespuestamedidor($1)", { sqlvalues });//aqui guardarlo a peticionesxx
                    execute_sql("select * from dlms.guardarrespuestamedidor($1::varchar[])", sqlvalues);
                    //printf("%s peticion perfil 1 OK : %s\n", m_codigomedidor.c_str(), restotal.c_str());
                    restotal.clear();
                }
                else
                {
                    sqlvalues_batch = sqlvalues_batch.substr(0, sqlvalues_batch.length() - 1);
                    sqlvalues_batch = "{" + sqlvalues_batch + "}";
                    //basedatos.SConsulta("select * from dlms.guardarrespuestamedidor($1)", { sqlvalues });//aqui guardarlo a peticionesxx
                    execute_sql("select * from dlms.guardarlecturamedidor_batch($1::varchar[][],$2)", sqlvalues_batch,m_idcomando);
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
                    sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_codigomedidor + "," + res + "}";
                    sqlvalues_batch += sqlvalues + ",";
                    //execute_sql("select * from dlms.guardarlecturamedidor($1::varchar[])", sqlvalues);
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
                //basedatos.SConsulta("select * from dlms.guardarrespuestamedidor($1)", { sqlvalues });//aqui guardarlo a peticionesxx
                execute_sql("select * from dlms.guardarrespuestamedidor($1::varchar[])", sqlvalues);
                restotal.clear();
            }
            else
            {
                sqlvalues_batch = sqlvalues_batch.substr(0, sqlvalues_batch.length() - 1);
                sqlvalues_batch = "{" + sqlvalues_batch + "}";
                cout << sqlvalues_batch << std::endl;
                execute_sql("select * from dlms.guardarlecturamedidor_batch($1::varchar[][],$2)", sqlvalues_batch, m_idcomando);//aqui mismo actualiza a estado 5
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
                sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_codigomedidor + "," + res + "}";
                sqlvalues_batch += sqlvalues + ",";
                //basedatos.SConsulta("select * from dlms.guardarlecturamedidor($1)", { sqlvalues });  Sincronico
                execute_sql("select * from dlms.guardarlecturamedidor($1::varchar[])", sqlvalues);
                //co_await execute_query_async_coro("select * from dlms.guardarlecturamedidor($1::varchar[])", sqlvalues);
                //printf("%s perfil 1 OK : %s\n", m_codigomedidor.c_str(), sqlvalues.c_str());

            }
            res.clear();
            sqlvalues.clear();
            m_statusprofile = 5;
        }
        if (error == 0)
        {
            sqlvalues_batch.pop_back();
            sqlvalues_batch = "{" + sqlvalues_batch + "}";
            execute_sql("select * from dlms.guardarlecturamedidor_batch($1::varchar[][],$2)", sqlvalues_batch, m_idcomando);
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
        /*std::string idcomando;
        std::stringstream st;
        st << m_idcomando;
        idcomando = st.str();
        return basedatos.SConsulta("select * from dlms.actualizarestadocomando($1,$2)", { idcomando,std::to_string(estado) });
        */
        execute_sql("select * from dlms.actualizarestadocomando($1,$2)", m_idcomando, estado);
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
    m_dlmsstate = 8;
    return 0;
}

void Session::UpdateDBMeterEstatus(int status)
{
    std::string sql = "select * from dlms.updatemeterstatus($1,$2)";
    //basedatos.SConsulta(sql, { m_codigomedidor,std::to_string(status) });
    execute_sql("select * from dlms.updatemeterstatus($1,$2)", m_codigomedidor, std::to_string(status));
    /*
    asio::co_spawn(socket_.get_executor(),
        [self = shared_from_this(),status]() -> boost::asio::awaitable<void> {
            try {
                // Ejecuta la consulta asincrónicamente sin resultados
                co_await self->execute_query_async_coro("select * from dlms.updatemeterstatus($1, $2)",self->m_codigomedidor,status);
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
        if (m_codigomedidor.empty() || m_codigomedidor == "")
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

                m_dlmsstate = 99;

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


    //m_Parser->SetProposedConformance((DLMS_CONFORMANCE)( DLMS_CONFORMANCE_PRIORITY_MGMT_SUPPORTED | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_GET | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION
    //    ));
    //m_Parser->SetNegotiatedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_PRIORITY_MGMT_SUPPORTED | DLMS_CONFORMANCE_ATTRIBUTE_0_SUPPORTED_WITH_GET | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION
    //    ));

    //m_Parser->SetNegotiatedConformance((DLMS_CONFORMANCE)(DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_GET_OR_READ | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_SET_OR_WRITE | DLMS_CONFORMANCE_BLOCK_TRANSFER_WITH_ACTION | DLMS_CONFORMANCE_MULTIPLE_REFERENCES | DLMS_CONFORMANCE_INFORMATION_REPORT | DLMS_CONFORMANCE_GET | DLMS_CONFORMANCE_SET | DLMS_CONFORMANCE_SELECTIVE_ACCESS | DLMS_CONFORMANCE_EVENT_NOTIFICATION | DLMS_CONFORMANCE_ACTION));
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
    asio::async_write(socket_, asio::buffer(data.data(), data.size()),
        [this, self, data = std::move(data)](std::error_code ec, std::size_t) {
            if (!ec) {
                //std::cout << "[OK]" << "\n";

            }
            else {
                std::cout << "Error enviando a " << m_codigomedidor << "\n";
            }
        });
}

void Session::SendInvocationCounter()
{
    if (m_initialization == 0)
    {
        std::string strobis = "0.0.43.1.0.255";
        CGXDLMSData* obj = new CGXDLMSData(strobis);

        if (obj != NULL)
        {
            auto aarqFrames = Read(obj, 2);
            for (auto& f : aarqFrames) {
                ReadDLMSPacket(f);
            }
            m_dlmsstate = 101;
        }
        else
        {
            printf("Unknown object: %s", strobis.c_str());
        }

    }
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
    //std::string sql = "select m_comando::text,m_fecha::text,m_fechaterminacion::text,m_idcomando,m_accion::text,m_tipo,m_medidor::text,m_perfil::text,m_escala::text,m_subtipo,m_error from dlms.comandosxejecutarDCU($1::varchar,3)";
    boost::asio::co_spawn(socket_.get_executor(),
        [self = shared_from_this()]() -> asio::awaitable<void> {
            try
            {
                auto res = co_await self->conn_->execute_query("select m_comando::text,m_fecha,m_fechaterminacion,m_idcomando,m_accion,m_tipo,m_medidor,m_perfil,m_escala,m_subtipo,m_error from dlms.comandosxejecutarDCU($1::varchar,3)", { self->m_concentrator });
                if (res.countRows() > 0) {
                    std::string opc = res.get(0, 0);
                    self->m_codigomedidor = res.get(0, 6);
                    self->m_relecturas = atoi(res.get(0, 5).c_str());
                    self->m_typecommand = self->m_comands[opc];
                    self->m_idcomando = atol(res.get(0, 3).c_str());
                    self->m_accion = res.get(0, 4);
                    self->m_fechaini = res.get(0, 1);
                    self->m_fechafin = res.get(0, 2);
                    //ResultSet resp2 = basedatos.SConsulta2(sql2, 1, resp.get(0, 7).c_str());
                    self->m_OBISobjects.clear();
                    self->m_ScalerObjects.clear();
                    Explode(res.get(0, 7).c_str(), ",", &self->m_OBISobjects);
                    Explode(res.get(0, 8).c_str(), ",", &self->m_ScalerObjects);
                    self->m_subtipo = (int)atol(res.get(0, 9).c_str());//para determinar si es macro o medidor virtual
                    self->m_numrepeticion = (int)atol(res.get(0, 10).c_str());//para determinar el numero de veces que se intento el comando
                    if (self->m_typecommand >= 100 && self->m_typecommand <= 200)//comandos rf inhemeter
                    {
                        if (self->m_inprogress == 0)
                        {
                            //m_initialization = 0;
                            self->ProcessComand(self->m_typecommand);//**FALTA** comandos rf inhemeter
                        }
                    }
                    else
                    {
                        if (self->m_subtipo == 0)//macroconcentrador
                        {
                            if (self->m_inprogress == 0)
                            {
                                self->m_initialization = 0;
                                self->m_typeinterface = 0;
                                // **NOTA IMPORTANTE SOBRE 'new' - Ver abajo**
                                //self->m_Parser = new CGXDLMSSecureClient(true, 0x12, 1);
                                self->m_Parser = std::make_unique<CGXDLMSSecureClient>(true, 0x12, 1);
                                self->m_Parser->SetServiceClass(DLMS_SERVICE_CLASS::DLMS_SERVICE_CLASS_CONFIRMED);
                                self->m_metertype = 0;
                                //self->ProcessComand(self->m_typecommand); **FALTA
                            }

                        }
                        else //medidor
                        {
                            if (self->m_inprogress == 0)
                            {
                                //DLMS_INTERFACE_TYPE tp = m_Parser->GetInterfaceType();
                                self->InitConnectionMeterDcuINH();

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
        asio::detached);
}

void Session::ProcessComand(int opc)
{
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
        tmp = m_codigomedidor + "(RF1) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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
        tmp = m_codigomedidor + "(RF2) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF3) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF4) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF5) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF6) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF7) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF8) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF9) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF10) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF11) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF12) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF13) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF14) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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

        tmp = m_codigomedidor + "(RF15) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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
        tmp = m_codigomedidor + "(RF16) TX:\t" + tmp + "\t" + GXHelpers::BytesToHex(datos.data(), (int)datos.size(), false);
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
    }
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

void Session::InitConnectionMeterDcuINH()
{
    int ret = 0;
    //deshabilitado temporalmente, realizar ppruebas para medidor que envia el contador de frames
    /*if ((ret = UpdateFrameCounter()) != 0)
    {
        return ret;
    }
    */
    m_inprogress = 1;

    //printf("%s : InitializeConnection\n", m_codigomedidor.c_str());
    std::string sqlvalues = "{" + m_codigomedidor + "," + "InitializeConnection,0}";
    execute_sql("select * from dlms.monitormedidor($1::varchar[])", sqlvalues);
    std::vector<CGXByteBuffer> data;
    //Get meter's send and receive buffers size.
    m_typeclcosem = 6;
    InitClientCosem();//cada que inicializa un medidor actualiza los parametros del m_parser
    m_timecommand = time(NULL);
    SNRMRequestINH(data);
    //ret = m_Parser->SNRMRequest(data);
    
    ret = ReadDataBlock(data);
    if (ret == 0 && data.size() == 0)
    {
        ret = m_Parser->ParseUAResponse(m_reply.GetData());
        if (ret == 0)
        {
            m_reply.Clear();
            ret = m_Parser->AARQRequest(data);
            ret = ReadDataBlock(data);
            m_dlmsstate = 2;
        }
    }
    else
    {
        m_dlmsstate = 1;

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
        m_dlmsstate = 11;
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
                        //basedatos.SConsulta("select * from dlms.guardarrespuestamedidor($1)", { sqlvalues });//aqui guardarlo a peticionesxx
                        execute_sql("select * from dlms.guardarrespuestamedidor($1::varchar[])", sqlvalues);
                    }
                    else
                    {
                        sqlvalues = Implode(",", valuesvi);
                        sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_codigomedidor + "," + DateTimeNow() + "," + sqlvalues + "}";
                        //basedatos.SConsulta("select * from dlms.guardarlecturamedidor($1)", { sqlvalues });
                        cout << "values: " << sqlvalues << std::endl;
                        execute_sql("select * from dlms.guardarlecturamedidor($1::varchar[])", sqlvalues);
                        m_statusprofile = 1;

                    }
                    UpdateCommandState(5);
                }
                else
                {
                    sqlvalues = Implode(",", valuesvi);
                    sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_codigomedidor + "," + DateTimeNow() + "," + sqlvalues + "}";
                    execute_sql("select * from dlms.guardarlecturamedidor($1)", sqlvalues);
                    //este es para S02
                    //sqlvalues = "{" + std::to_string(m_numperfil) + "," + std::to_string(m_metertype) + "," + m_codigomedidor + "," + res + "}";
                    //basedatos.SConsulta("select * from dlms.guardarlecturamedidor($1)", { sqlvalues });
                    /*if (schemas.size() == 1) soporte para multiples bases **FALTA**
                    {
                        sql = "select * from dlms.guardar_lecturassubestacion($1)";
                    }
                    else
                    {
                        sql = "select * from dlms.guardar_lecturassubestacion($1)";
                        sql1 = "select * from dlms.guardar_lecturassubestacion($1)";
                    }*/
                    m_statusprofile = 1;
                    //es automatico por la tanto es una lectura que se guarda en la tabla lecturas
                }
                valuesvi.clear();
                m_instantaneousvalues.clear();
                m_dlmsstate = 0;
            }
            else
            {
                m_instantaneousvalues.clear();
                m_dlmsstate = 0;
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
        m_dlmsstate = 20;

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
        m_dlmsstate = 9;//por definir
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
        m_dlmsstate = 50;
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
        m_dlmsstate = 60;
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
        Explode(captureobjects, ",", &obisvec);
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
#if HAS_CPP20_COROUTINES
asio::awaitable<int> Session::GetParamsFromIP_coro(const std::string& ip, int type)
{
    try {
        if (type == 0) {
            std::string sql = "SELECT codigoconcentrador::text FROM principal.concentradores WHERE direccionip=$1";
            auto result = co_await conn_->execute_query(sql, { ip });
			//ResultSet result = co_await conn_->execute_query(sql, { ip });
            if (result.countRows()> 0) {
                const auto& concentrador = result.get(0,0);
                m_concentrator = concentrador;
                printf("Concentrador encontrado en base: %s\n", m_concentrator.c_str());

                if (on_device_identified_) {
                    on_device_identified_(m_concentrator);
                }
                co_return 0;
            }
            else {
                printf("Concentrador no encontrado para IP %s\n", ip.c_str());
                co_return -1;
            }
        }
        else {
            std::string sql = "SELECT codigomedidor, clientecosem, perfilvi, escalasvi, contrasena, authkey, cipherkey, dedkey FROM dlms.obtenerparametrosmedidor($1)";
            auto result = co_await conn_->execute_query(sql, { ip });

            if (result.countRows()>0 && (result.get(0,0) != "-1"))
            {
                const auto& row = result.get(0);
                m_codigomedidor = row[0];
                m_typeclcosem = std::stoi(row[1]);
                Explode(row[2].c_str(), ",", &m_OBISobjects);
                Explode(row[3].c_str(), ",", &m_ScalerObjects);
                m_passwordmeter = row[4];
                m_authenticationkey = row[5];
                m_cipherkey = row[6];
                m_dedicatekey = row[7];

                printf("Medidor encontrado en base: %s\n", m_codigomedidor.c_str());

                if (on_device_identified_) {
                    on_device_identified_(m_codigomedidor);
                }
                co_return 0;
            }
            else {
                printf("Medidor no encontrado para IP %s\n", ip.c_str());
                co_return -1;
            }
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
        auto res = co_await conn_->execute_query("select * from dlms.CambiarPerfilxError($1)", { sqlvalues });
        if (res.countRows() > 0)
        {
            std::string dato = res.get(0, 0);
            if (!dato.empty())
            {
                m_ScalerObjects.clear();
                Explode(dato, ",", &m_ScalerObjects);//actualizo esanl,dato.c_str()a; en el socket
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
            auto res = co_await conn_->execute_query("select escala from dlms.perfileslecturasvi where idperfil=$1", { std::to_string(m_numperfil) });
            if (res.countRows() > 0)
            {
                std::string dato = res.get(0, 0);
                if (!dato.empty())
                {
                    m_ScalerObjects.clear();
                    Explode(dato, ",", &m_ScalerObjects);//actualizo esanl,dato.c_str()a; en el socket
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
            auto res = co_await conn_->execute_query("select escala from dlms.perfileslecturasvi where idperfil=$1", { std::to_string(m_numperfil) });
            if (res.countRows() > 0)
            {
                std::string dato = res.get(0, 0);
                if (!dato.empty())
                {
                    m_ScalerObjects.clear();
                    Explode(dato, ",", &m_ScalerObjects);//actualizo esanl,dato.c_str()a; en el socket
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
#endif