#pragma once
#include <vector>
#include <string>
//clase para protocolo Q/gdw 371.2 2009 usado por concentradores inh
class Qgdw
{
public:
	Qgdw();
	~Qgdw();
	/// <summary>
	/// construye la cabecera del frame de cruedo al tamaño de los datos enviados
	/// </summary>
	/// <param name="longitud"></param>
	std::vector<unsigned char> HeaderFrame(int longitud);
	std::vector<unsigned char> DomainCtrl();
	std::vector<unsigned char> FrameAddress(std::string concentrador,std::string byteadd="04");
	std::vector<unsigned char> AFN();
	std::vector<unsigned char> SEQ();
	std::vector<unsigned char> DA();
	std::vector<unsigned char> DT();
	std::vector<unsigned char>UNKOWN1();
	std::vector<unsigned char>UNKOWN2(int dlmsstate);
	std::vector<unsigned char>Data1(std::string concentrator, std::string meter,int dlmsstate);
	/// <summary>
	/// similar a data1 pero la construccion del frame es automatico de acuerdo a la longitud del dlms
	/// </summary>
	/// <param name="concentrator"></param>
	/// <param name="meter"></param>
	/// <param name="len"></param>
	/// <returns></returns>
	std::vector<unsigned char>Data0(std::string concentrator, std::string meter, int len);
	std::vector<unsigned char>Data0x(const std::string& concentrator, const std::string& meter, int len);

	std::vector<unsigned char>DmlsData(std::string datahex);
	/// <summary>
	/// genere el frame auxiliar para macroconcentrador
	/// </summary>
	/// <returns></returns>
	std::vector<unsigned char>AuxData0();
	std::vector<unsigned char>AuxData(std::vector<unsigned char>checksumdata1);
	std::vector<unsigned char>CheckSum(std::vector<unsigned char> data);
	unsigned char EndFrame() { return 0x16; };
	void TestFrame();
	std::vector<unsigned char> PacketFrame(std::string dlms,std::string concentrator,std::string meter,int dlmsstate);
	/// <summary>
	/// version mejorada de packetframe, no necesitadlms state ya que se generan automaticamente de acuerdo
	/// a la longitud de bloques del frame
	/// </summary>
	/// <param name="dlms"></param>
	/// <param name="concentrator"></param>
	/// <param name="meter"></param>
	/// <returns></returns>
	std::vector<unsigned char> PacketFrame2(std::string dlms, std::string concentrator, std::string meter);

	std::vector<unsigned char> HBFrame(std::string concentrator,std::vector<unsigned char> hb);
	int GetFrameSize() { return m_lenframe; };
	std::vector<unsigned char> DecodeFrame(const unsigned char* data, int len);
	std::string GetConcentrator() { return m_concentrador; };
	/// <summary>
	/// determina el tipo de frame si es dlms o rf de escritura o lectura
	/// </summary>
	/// <returns></returns>
	int GetSubframeType() { return m_typeframe; }
	/// <summary>
	/// determina el tipo de frame que recibe
	/// 00 desconocido
	/// 01 heartbeat
	/// 02 dlms
	/// 03 event rf
	/// </summary>
	/// <returns></returns>
	int Getframetype() { return m_frametype; };
	//Funciones RF
	//1- Stop reading task(CON04050W)
    std::vector<unsigned char> StopReadingTask(std::string concentrator);
	//2- clear measurement point(CON00107W);
	std::vector<unsigned char> ClearMeasurementPoint(std::string concentrator);
	//3- Clear Task Params,Cmd:4764e044ee-7-000 CON04041W (204100001|0|00|0);
	std::vector<unsigned char> ClearTask(std::string concentrator);
	//4- Set F33, CmdCode:4764e044ee - 71 - 01 CON04007W
	std::vector<unsigned char> F33(std::string concentrator);
	//5- Set F38, CmdCode:4764e044ee - 71 - 01 CON04038W
	std::vector<unsigned char> F38(std::string concentrator);
	//6- Set F39 ,CmdCode:4764e044ee-71-01 CON04039W
	std::vector<unsigned char> F39(std::string concentrator);
	//7- Set F40 ,CmdCode:4764e044ee-71-01 CON04040W
	std::vector<unsigned char> F40(std::string concentrator);
	std::vector<unsigned char> ReadRfVersion(std::string concentrator);
	std::vector<unsigned char> ReadNetworkParameter(std::string concentrator);
	/// <summary>
	/// parametriza parametros de red rf
	/// opc=0 canal
	/// opc=1 netid
	/// opc=2 frecuencia
	/// </summary>
	/// <param name="concentrator"></param>
	/// <param name="opc"></param>
	/// <param name="value"></param>
	/// <returns></returns>
	std::vector<unsigned char> SetNetworkParameter(std::string concentrator,int opc,int value);


	std::vector<unsigned char> AddNetPoint(std::string concentrator, std::string meters);
	std::vector<unsigned char> AuxDataMP(std::vector<unsigned char>checksumdata1);
	std::vector<unsigned char> SetMeasurementPoint(std::string concentrator, std::string meters,int ini=0,int pending=0,int step=0);
	std::vector<unsigned char> AddBroadcastPointNumber(std::string concentrator, std::string meters);
	std::vector<unsigned char> StartRfNetworking(std::string concentrator);
	/// <summary>
	/// la respuesta de esta funcion devuelve los medidores que se enlazaron en la red rf
	/// </summary>
	/// <param name="concentrator"></param>
	/// <param name="cont"></param>
	/// <param name="nummeters"></param>
	/// <returns></returns>
    std::vector<unsigned char> ReadNodeLevel(std::string concentrator,int cont,int nummeters);
	/// <summary>
	/// Cierra el proceso de construccion de la red RF
	/// </summary>
	/// <param name="concentrator"></param>
	/// <returns></returns>
	std::vector<unsigned char>StopBuild(std::string concentrator);
	/// <summary>
	/// inicia tarea
	/// </summary>
	/// <param name="concentrator"></param>
	/// <returns>byte array</returns>
	std::vector<unsigned char>StartTask(std::string concentrator);
	std::vector<unsigned char>VerifyDC(std::string concentrator);
    std::string ParseEvents(std::vector<unsigned char> data);
private:
	int m_contador = 1;//0-15 //para trama SEQ
	//int m_contador2 = 1;//0-255
	int m_contaux1 = 255;
	int m_contaux2 = 1;
	int m_lenframe = 0;
	int m_frametype = 0;
	int m_typeframe = 0;
	std::string m_concentrador;
	//variables rf
	//int p1 = 3;//variable para generar los bloques de medidores en setmeasurementpoint
	//int t2 = 1;
	//int t1 = 2;
	//std::vector<int> vecMeasurePoint1 = { 2,2,3,3,3,4,4,4,5,5,5,6,6,6,7,7,7,8,8,0 };
	//std::vector<int> vecMeasurePoint2 = { 2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,0 };
};

