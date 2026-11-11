#pragma once
#include <vector>
#include <string>
//Q/gdw 371.2 2009
class Qgdw
{
public:
	Qgdw();
	~Qgdw();
	std::vector<unsigned char> HeaderFrame(int longitud);
	std::vector<unsigned char> DomainCtrl();
	std::vector<unsigned char> FrameAddress(std::string concentrador,std::string byteadd="04");
	std::vector<unsigned char> AFN();
	std::vector<unsigned char> SEQ();
	std::vector<unsigned char> DA();
	std::vector<unsigned char> DT();
	std::vector<unsigned char>InhDlmsWrapper();
	std::vector<unsigned char>Data0(const std::string& concentrator, const std::string& meter, int len);
	std::vector<unsigned char>DmlsData(std::string datahex);
	std::vector<unsigned char>AuxData0();
	std::vector<unsigned char>AuxData(std::vector<unsigned char>checksumdata1);
	std::vector<unsigned char>CheckSum(std::vector<unsigned char> data);
	unsigned char EndFrame() { return 0x16; };
	std::vector<unsigned char> PacketFrame2(std::string dlms, std::string concentrator, std::string meter);
	std::vector<unsigned char> HBFrame(std::string concentrator,std::vector<unsigned char> hb);
	int GetFrameSize() { return m_lenframe; };
	std::vector<unsigned char> DecodeFrame(const unsigned char* data, int len);
	std::string GetConcentrator() { return m_concentrador; };
	int GetSubframeType() { return m_typeframe; }
	int Getframetype() { return m_frametype; };
    std::vector<unsigned char> StopReadingTask(std::string concentrator);
	std::vector<unsigned char> ClearMeasurementPoint(std::string concentrator);
	std::vector<unsigned char> ClearTask(std::string concentrator);
	std::vector<unsigned char> F33(std::string concentrator);
	std::vector<unsigned char> F38(std::string concentrator);
	std::vector<unsigned char> F39(std::string concentrator);
	std::vector<unsigned char> F40(std::string concentrator);
	std::vector<unsigned char> ReadRfVersion(std::string concentrator);
	std::vector<unsigned char> ReadNetworkParameter(std::string concentrator);
	std::vector<unsigned char> SetNetworkParameter(std::string concentrator,int opc,int value);
	std::vector<unsigned char> AddNetPoint(std::string concentrator, std::string meters);
	std::vector<unsigned char> AuxDataMP(std::vector<unsigned char>checksumdata1);
	std::vector<unsigned char> SetMeasurementPoint(std::string concentrator, std::string meters,int ini=0,int pending=0,int step=0);
	std::vector<unsigned char> AddBroadcastPointNumber(std::string concentrator, std::string meters);
	std::vector<unsigned char> StartRfNetworking(std::string concentrator);
    std::vector<unsigned char> ReadNodeLevel(std::string concentrator,int cont,int nummeters);
	std::vector<unsigned char>StopBuild(std::string concentrator);
	std::vector<unsigned char>StartTask(std::string concentrator);
	std::vector<unsigned char>VerifyDC(std::string concentrator);
    std::string ParseEvents(std::vector<unsigned char> data);
private:
	int m_contador = 1;//0-15 //para trama SEQ
	int m_contaux1 = 255;
	int m_contaux2 = 1;
	int m_lenframe = 0;
	int m_frametype = 0;
	int m_typeframe = 0;
	std::string m_concentrador;
};

