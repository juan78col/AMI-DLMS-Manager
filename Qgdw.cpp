#include "Qgdw.h"
#include <string>
#include <bitset>
#include "funciones.h"
#include<iterator>
#include<cmath>
//#include <iostream>
template <typename T>
std::vector<T> operator+(std::vector<T> const& x, std::vector<T> const& y)
{
	std::vector<T> vec;
	vec.reserve(x.size() + y.size());
	vec.insert(vec.end(), x.begin(), x.end());
	vec.insert(vec.end(), y.begin(), y.end());
	return vec;
}

template <typename T>
std::vector<T>& operator+=(std::vector<T>& x, const std::vector<T>& y)
{
	x.reserve(x.size() + y.size());
	x.insert(x.end(), y.begin(), y.end());
	return x;
}
Qgdw::Qgdw()
{
}

Qgdw::~Qgdw()
{
}

std::vector<unsigned char> Qgdw::HeaderFrame(int longitud)
{
	std::string binary = std::bitset<14>((unsigned long long)longitud).to_string() + "10"; //to binary se puede agregar 254 para añadir el 10 bit
	// convert to long little endian hex
	std::bitset<32> bit(binary);
	long lNum = (long)bit.to_ulong();// (long)__bswap_64(bit.to_ulong());
	std::vector<unsigned char>lendata = HexToBytes(IntToHex((int)lNum));
	if (lendata.size() == 1)
	{
		lendata.push_back(0x00);
	}
	else
	{
		std::reverse(lendata.begin(), lendata.end());

	}
	std::vector<unsigned char>dataheader;
	dataheader.push_back(0x68);
	dataheader.insert(dataheader.end(), lendata.begin(), lendata.end());
	dataheader.insert(dataheader.end(), lendata.begin(), lendata.end());
	dataheader.push_back(0x68);
	return dataheader;

}

std::vector<unsigned char> Qgdw::DomainCtrl()
{
	//d7: bit de dirección de transmisión DIR
	//0 indica que este mensaje de trama es un mensaje de enlace descendente enviado por la estación maestra
	//1 indica que este mensaje de trama es un mensaje de enlace ascendente enviado por el terminal
	//d6 : inicia el bit de bandera PRM
	//0 significa que este mensaje de trama proviene de la estación esclava
	//1 significa que este mensaje de trama proviene de la estación de inicio
	//d5: bit de conteo de tramas FCB
	//d5: ACD=1 indica que el terminal tiene eventos importantes esperando ser accedidos, luego el campo de información adicional tiene un contador de eventos EC (ver 4.3.4.6.3 de esta sección); 
	//ACD= 0 significa que el terminal no tiene datos de eventos a la espera de ser accedidos.
	//d4: reservado
	//d3～d0: código de función,cuando el bit de bandera de inicio PRM=0, significa que el tipo de trama del mensaje de trama es de solicitud/respuesta, la función de servicio es prueba de enlace, y es utilizado para AFN=02 El código de función de la capa de aplicación de
	//ej 0100 1011 =>4B el bit que siempre se manda en concentradores, hasta ahora
	//mensaje enviado por la estacion maestra,trama de estacion de inicio,no tiene datos de eventos,solicitud de envio y respuesta
	std::vector<unsigned char> resp;
	resp.push_back(0x4B);
	return resp;
}

std::vector<unsigned char> Qgdw::FrameAddress(std::string concentrador, std::string byteadd)
{
	int num1 = atoi(concentrador.substr(7, 2).c_str());
	std::string framedir = concentrador.substr(2, 2) + concentrador.substr(0, 2) + IntToHex(num1) + concentrador.substr(5, 2) + byteadd;
	return HexToBytes(framedir);
	//212000005 => 2021050004
}

std::vector<unsigned char> Qgdw::AFN()
{
	std::vector<unsigned char> fcp;
	fcp.push_back(0x10);
	return fcp;
}

std::vector<unsigned char> Qgdw::SEQ()
{
	//D7   D6     D5     D4      D3-D0
	//TPV  ABETO  ALETA  ESTAFA  PSEQ / RSEQ
	//1    1      1      0       1111
	//TpV:bit de validez de etiqueta de tiempo de trama
	//TpV=0 indica que no hay etiqueta de tiempo Tp en el campo de información adicional 
	//TpV=1, que indica que hay etiqueta de tiempo Tp en el campo de información adicional
	//abeto aleta 1-1 un solo cuadro o trama
	//d4 0 no necesita confirmacion del mensaje
	//d3-d0 contador ciclico de 0 a 15
	if (m_contador > 15)
	{
		m_contador = 0;
	}
	std::string sq = "1110" + std::bitset<4>((unsigned long long)m_contador).to_string();
	std::bitset<8> sq2(sq);
	int contador = (int)sq2.to_ulong();
	std::vector<unsigned char>vecsq;

	vecsq.push_back((unsigned char)contador);
	m_contador++;
	return vecsq;
}

std::vector<unsigned char> Qgdw::DA()
{
	std::vector<unsigned char> da1;
	da1.push_back(0x00);
	da1.push_back(0x00);
	//si es 00 00 es el punto de informacion dle terminal inicial
	return da1;
}

std::vector<unsigned char> Qgdw::DT()
{
	std::vector<unsigned char> dt1;
	dt1.push_back(0x01);
	dt1.push_back(0x00);
	//si es 00 00 es el punto de informacion dle terminal inicial
	return dt1;
}

std::vector<unsigned char> Qgdw::UNKOWN1()
{
	std::vector<unsigned char> dt1;
	dt1.push_back(0x1F);
	dt1.push_back(0xCB);
	//si es 00 00 es el punto de informacion dle terminal inicial
	return dt1;
}

std::vector<unsigned char> Qgdw::UNKOWN2(int dlmsstate)
{
	unsigned char contador;
	switch (dlmsstate)
	{
	case 0://snmr
	{
		contador = 0x3D;
		break;
	}
	case 1://aarq
	{
		contador = 0x6A;
		break;
	}
	case 2:// 1pet
	{
		contador = 0x38;
		break;
	}
	case 3:
	{
		contador = 0x6B;
		break;
	}
	case 4://2 pet
	{
		contador = 0x26;
		break;
	}
	case 5://
	{
		contador = 0x32;
		break;
	}
	case 6://para peticiones de escritura
	{
		contador = 0x3A;
		break;
	}
	default:
		break;
	}
	//A8 14 6B 00 68 6B
	std::vector<unsigned char> dt1 = { 0xA8,0x14 };
	dt1.push_back(contador);
	dt1.push_back(0x00);
	dt1.push_back(0x68);
	dt1.push_back(contador);
	return dt1;
}

std::vector<unsigned char> Qgdw::Data1(std::string concentrator, std::string meter, int dlmsstate)
{
	//00410500000000000700202100002400172972030201000020
	//212000007
	std::string contador;
	switch (dlmsstate)
	{
	case 0://snmr frame
	{
		contador = "20";
		break;
	}
	case 1://aarq
	{
		contador = "4D";
		break;
	}
	case 2:// 1pet
	{
		contador = "1B";  //hasta aqui para peticiones normales
		break;
	}
	case 3://2 pet //profile generic
	{
		contador = "4E";
		break;
	}
	case 4://read next frame cuando se pide profiles
	{
		contador = "09"; //s frame
		break;
	}
	case 5:
	{
		contador = "15";//next data block frame
		break;
	}
	case 6://para peticiones de escritura
	{
		contador = "1D";
		break;
	}
	default:
		break;
	}
	std::string invcon = concentrator.substr(2, 2) + concentrator.substr(0, 2);
	int num1 = atoi(concentrator.substr(7, 2).c_str());
	std::string meter1, invmeter;
	meter1 = "0" + meter;
	//52 89 36 41 72 03 020100004D
	//037241368952
	for (size_t i = 6; i >= 0; i--)
	{
		invmeter.append(meter1.substr(i * 2, 2).c_str());
	}
	std::string sthex = "0041050000000000" + IntToHex(num1) + "00" + invcon + "0000" + invmeter + "02010000" + contador;
	return HexToBytes(sthex);
	//212000005 => 2021050 004

}

std::vector<unsigned char> Qgdw::Data0(std::string concentrator, std::string meter, int len)
{
	//00410500000000000700202100002400172972030201000020
	//212000007
	std::string invcon = concentrator.substr(2, 2) + concentrator.substr(0, 2);
	//int num1 = atoi(concentrator.substr(7, 2).c_str());
	int num1 = std::stoi(concentrator.substr(7, 2));
	std::string meter1, invmeter;
	meter1 = "0" + meter;
	//52 89 36 41 72 03 020100004D
	//037241368952
	for (int i = 5; i >= 0; i--)
	{
		invmeter.append(meter1.substr((size_t)i * 2, 2));
	}
	//std::cout << "invmeter:" << invmeter << std::endl;
	std::string sthex = "0041050000000000" + IntToHex(num1) + "00" + invcon + "0000" + invmeter + "02010000" + IntToHex(len);
	return HexToBytes(sthex);
	//212000005 => 2021050 004

}

std::vector<unsigned char> Qgdw::Data0x(const std::string& concentrator, const std::string& meter, int len)
{
	if (concentrator.size() < 9)
		throw std::invalid_argument("concentrator demasiado corto");

	std::vector<unsigned char> data;
	data.reserve(32);

	// 1️⃣ Cabecera fija
	const unsigned char header[] = {
		0x00, 0x41, 0x05, 0x00,
		0x00, 0x00, 0x00, 0x00
	};
	data.insert(data.end(), std::begin(header), std::end(header));

	// 2️⃣ Número del concentrador (byte)
	int num1 = (concentrator[7] - '0') * 10 + (concentrator[8] - '0');
	data.push_back(static_cast<unsigned char>(num1));

	// 3️⃣ 00 separador
	data.push_back(0x00);

	// 4️⃣ invcon (dos pares de bytes invertidos)
	data.push_back(static_cast<unsigned char>(concentrator[2]));
	data.push_back(static_cast<unsigned char>(concentrator[3]));
	data.push_back(static_cast<unsigned char>(concentrator[0]));
	data.push_back(static_cast<unsigned char>(concentrator[1]));

	// 5️⃣ 0000
	data.push_back(0x00);
	data.push_back(0x00);

	// 6️⃣ invmeter (pares invertidos)
	std::string meter_temp = meter;
	if (meter_temp.size() % 2 != 0)
		meter_temp.insert(0, "0");
	const size_t pairs = meter_temp.size() / 2;
	for (size_t i = pairs; i-- > 0;) {
		unsigned char hi = meter_temp[i * 2];
		unsigned char lo = meter_temp[i * 2 + 1];
		data.push_back(hi);
		data.push_back(lo);
	}

	// 7️⃣ 02010000
	const unsigned char tail[] = { 0x02, 0x01, 0x00, 0x00 };
	data.insert(data.end(), std::begin(tail), std::end(tail));

	// 8️⃣ len (1 byte)
	data.push_back(static_cast<unsigned char>(len));

	return data;
}

std::vector<unsigned char> Qgdw::DmlsData(std::string datahex)
{
	std::string tmp = datahex;
	return HexToBytes(tmp);
}

std::vector<unsigned char> Qgdw::AuxData0()
{
	if (m_contaux1 > 255)
	{
		m_contaux1 = 0;
	}
	if (m_contaux2 > 255)
	{
		m_contaux2 = 0;
	}
	std::vector<unsigned char> vecaux;
	vecaux.push_back((unsigned char)m_contaux2);
	m_contaux1++;
	m_contaux2++;
	struct tm now = GetLocaltime();
	//2023-06-26 19:34:34
	/*vecaux.push_back(0x34);
	vecaux.push_back(0x34);
	vecaux.push_back(0x19);
	vecaux.push_back(0x26);
	*/
	//real
	vecaux.push_back((unsigned char)HexToInt(std::to_string(now.tm_sec)));
	vecaux.push_back((unsigned char)HexToInt(std::to_string((now.tm_min))));
	vecaux.push_back((unsigned char)HexToInt(std::to_string(now.tm_hour)));
	vecaux.push_back((unsigned char)HexToInt(std::to_string(now.tm_mday)));

	vecaux.push_back(0x00);
	return vecaux;
}

std::vector<unsigned char> Qgdw::AuxData(std::vector<unsigned char>checksumdata1)
{
	if (m_contaux1 > 255)
	{
		m_contaux1 = 0;
	}
	if (m_contaux2 > 255)
	{
		m_contaux2 = 0;
	}
	std::vector<unsigned char> vecaux;
	vecaux.insert(vecaux.end(), checksumdata1.begin(), checksumdata1.end());
	//vecaux.push_back(m_contaux1);//aqui checksum de data1
	vecaux.push_back(0x16);
	vecaux.push_back((unsigned char)m_contaux2);
	m_contaux1++;
	m_contaux2++;
	struct tm now = GetLocaltime();
	//2022-07-12 16:11:48  2023-06-26 19:34:34
	//vecaux.push_back(0x34);
	//vecaux.push_back(0x34);
	//vecaux.push_back(0x19);
	//vecaux.push_back(0x26);
	//real
	vecaux.push_back((unsigned char)HexToInt(std::to_string(now.tm_sec)));
	vecaux.push_back((unsigned char)HexToInt(std::to_string((now.tm_min))));
	vecaux.push_back((unsigned char)HexToInt(std::to_string(now.tm_hour)));
	vecaux.push_back((unsigned char)HexToInt(std::to_string(now.tm_mday)));
	vecaux.push_back(0x00);

	//vecaux.push_back(0x43); vecaux.push_back(0x09); vecaux.push_back(0x11); vecaux.push_back(0x04); vecaux.push_back(0x00);
	//0D 30 24 19 04 00 =>2022-05-04 19:24:30
	return vecaux;
}

std::vector<unsigned char> Qgdw::CheckSum(std::vector<unsigned char> data)
{
	int sumelements = 0;
	for (std::vector<unsigned char>::iterator j = data.begin(); j != data.end(); ++j)
		sumelements += *j;
	sumelements = sumelements % 256;
	std::vector<unsigned char> tmp;
	tmp.push_back((unsigned char)sumelements);
	return tmp;
}

void Qgdw::TestFrame()
{
	std::string dlms = "7EA01E0303934E2B8180120501E60601E60704000000010804000000011C117E";
	std::vector<unsigned char>vectorT;
	std::vector<unsigned char> tmp, tmp2;
	tmp = DomainCtrl();
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();
	tmp = FrameAddress("204100015");
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();
	tmp = AFN();
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();
	tmp = SEQ();
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();
	tmp = DA();
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();
	tmp = DT();
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();
	tmp = UNKOWN1();
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();
	tmp = UNKOWN2(0);
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();
	tmp = Data1("204100015", "37229132750", 0);
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();
	tmp = HexToBytes(dlms);
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();
	tmp = AuxData(vectorT);
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	tmp.clear();

	tmp2 = HeaderFrame((int)vectorT.size());
	tmp = CheckSum(vectorT);
	vectorT.insert(vectorT.end(), tmp.begin(), tmp.end());
	vectorT.insert(vectorT.begin(), tmp2.begin(), tmp2.end());
	vectorT.push_back(EndFrame());
	for (unsigned char i : vectorT)
		printf("%.2X", i);
}

std::vector<unsigned char> Qgdw::PacketFrame(std::string dlms, std::string concentrator, std::string meter, int dlmsstate)
{
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	data1 = Data1(concentrator, meter, dlmsstate) + HexToBytes(dlms);
	crcdata1 = CheckSum(data1);
	Vectotal = DomainCtrl() + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + UNKOWN1() + UNKOWN2(dlmsstate) + data1 + AuxData(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::PacketFrame2(std::string dlms, std::string concentrator, std::string meter)
{
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comand1, lencomand2;
	int lencomand = 0;
	int ismacrodcu = 0;
	if (("37" + concentrator) == meter)
	{
		ismacrodcu = 1;
	}
	if (ismacrodcu == 0)
	{
		data1 = Data0(concentrator, meter, ((int)dlms.size() / 2)) + HexToBytes(dlms);
		crcdata1 = CheckSum(data1);
		comand1 = "1FCBA814"; //para macrococoncentrador,solo se pone la longitud del frame dlms xx00
		lencomand = (int)data1.size() + 4;
		lencomand2 = IntToHex(lencomand) + "0068" + IntToHex(lencomand);
		comand1 = comand1 + lencomand2;
		Vectotal = DomainCtrl() + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + HexToBytes(comand1) + data1 + AuxData(crcdata1);
		tmp = HeaderFrame((int)Vectotal.size());
		Vectotal = Vectotal + CheckSum(Vectotal);
		Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
		Vectotal.push_back(EndFrame());

	}
	else
	{
		//modifico el dmls manual aun no se obtien la condfiguracion correcta,pero para lecturas al macrodcu los bytes son los mismos
		if (dlms.substr(11, 5) == "01FBD")
		{
			dlms.replace(11, 5, "3848F");
		}
		if (dlms.substr(10, 6) == "324E04")
		{
			dlms.replace(10, 6, "13C534");
		}

		data1 = HexToBytes(dlms + "00000000000000000000000000000000");
		//data1 = HexToBytes("7EA019032513848FE6E600C001C100070100630100FF03008C907E00000000000000000000000000000000");
		crcdata1 = CheckSum(data1);
		comand1 = "01CBA314";
		lencomand = (int)dlms.size() / 2;
		comand1 = comand1 + IntToHex(lencomand) + "00";
		//std::vector<unsigned char> aux1 = { 0x01,0x34,0x34,0x19,0x26,0x00};
		Vectotal = DomainCtrl() + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + HexToBytes(comand1) + data1 + AuxData0();
		tmp = HeaderFrame((int)Vectotal.size());
		Vectotal = Vectotal + CheckSum(Vectotal);
		Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
		Vectotal.push_back(EndFrame());

	}
	m_lenframe = (int)Vectotal.size();
	/*
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	*/
	return Vectotal;
}

std::vector<unsigned char> Qgdw::HBFrame(std::string concentrator, std::vector<unsigned char>hb)
{
	std::vector<unsigned char>vectotal = { 0x0B };
	std::vector<unsigned char>tmp = { 0x00,0x00,0x04,0x00,0x02,0x00,0x00,hb[16],0x00,0x00 };
	std::vector<unsigned char>tmp2;
	vectotal += FrameAddress(concentrator, "00");
	vectotal.push_back(0x00);
	vectotal.push_back((unsigned char)(hb[13] - 0x10)); //16
	vectotal = vectotal + tmp;
	tmp2 = HeaderFrame((int)vectotal.size());
	vectotal = vectotal + CheckSum(vectotal);
	vectotal.insert(vectotal.begin(), tmp2.begin(), tmp2.end());
	vectotal.push_back(EndFrame());
	//683200320068 C9 2021070000 02 71 00 00 04 00 88 16 => pulso
	//684A004A0068 0B 2021070000 00 61 00 00 04 00 02 00 00 04 00 00 BE 16 => respuesta
	return vectotal;
}

std::vector<unsigned char> Qgdw::DecodeFrame(const unsigned char* data, int len)
{
	//0000000000110010
	std::vector<unsigned char> vecdata(data, data + len);//problemas en algunas versiones gcc
	std::vector<unsigned char> vectmp, vecdlms;
	vectmp.push_back(vecdata[2]); vectmp.push_back(vecdata[1]);
	int len1 = HexToInt(BytesToHex(vectmp).c_str());
	std::string strtmp = std::bitset<16>((unsigned long long)len1).to_string();
	strtmp = strtmp.substr(0, strtmp.size() - 2);
	int lenframeData = (int)std::bitset<14>(strtmp).to_ulong();//si la longiutd es 12 es un hearbeath
	std::string bitcero = "";
	if (vecdata[9] < 10)
	{
		bitcero = "0";
	}
	std::string concentrador = IntToHex(vecdata[8]) + IntToHex(vecdata[7]) + std::to_string(vecdata[10]) + "00" + bitcero + std::to_string(vecdata[9]);
	unsigned char domainctrl = vecdata[6];
	//respuesta g3plc
	//6862006200688947235B000400E100000400100000010003012859221000FF16
	switch (domainctrl)
	{
	case 0xC9://hb concentrador
	{
		m_concentrador = concentrador;
		vecdlms = HBFrame(concentrador, vecdata);
		m_frametype = 1;
		//std::cout << "[HB] " << std::endl;
		//enviar buffer de respuesta al pulso
		//68 32 00 32 00 68 C9 20 21 07 00 00 02 71 00 00 04 00 88 16 => pulso
		//68 4A 00 4A 00 68 0B 20 21 07 00 00 00 61 00 00 04 00 02 00 00 04 00 00 BE 16 => respuesta
		break;
	}
	case 0x88://datos de respuesta
	{
		if (vecdata[12] == 0x10)//usuario
		{
			if (vecdata[18] == 0x1F)//para determinar si es respuesta dlms de medidor 01 es respuesta dlms de macroconcentrador
			{
				m_typeframe = vecdata[25];
				if ((vecdata[25] == 0x05) ||(vecdata[25] == 0x04))//comando dlms al parecer 05 es para dcu con rf y 04 para dcu g3plc
				{
					vecdlms.insert(vecdlms.begin(), vecdata.begin() + 48, vecdata.end() - 10);
				}
				else if (vecdata[25] == 0x00)//comando rf
				{
					//std::string com1=ByteToString(vecdata.data(), 18, 7);
					//if (com1 == "1F11006811008B")// es una respuesta a un comando write, por lo tanto devuelve succes o error
					//{
					//
					//}
					vecdlms.insert(vecdlms.begin(), vecdata.begin() + 26, vecdata.end() - 10);
				}
				else if (vecdata[25] == 0x01)//respuesta de concentrador a peticion dlms,puede ser medidor no econtrado o offline
				{
					vecdlms.insert(vecdlms.begin(), vecdata.begin() + 32, vecdata.end() - 12);//02 00 0F meter refused -1921
					vecdlms.clear();
				}
			}
			if (vecdata[18] == 0x01)//dlms macroconcentrador
			{
				vecdlms.clear();
				//vecdata[20]+vecdata[19] puede ser la longitud del frame dlms
				vecdlms.insert(vecdlms.begin(), vecdata.begin() + 21, vecdata.end() - 8);
				std::string demo567 = BytesToHex(vecdlms);
				printf("dlms macrodcu: %s\n", demo567.c_str());
			}

		}
		else if (vecdata[12] == 0x00)//success
		{
			vecdlms.push_back(0x00);
			vecdlms.push_back(0x01);
		}
		m_frametype = 0;
		break;
	}
	case 0x89://datos de respuesta aqui manda error -1828, por ahor manda error cuando se manda un profile de un dia completo para macrodcu
	{
		if (vecdata[12] == 0x00)
		{
			if (vecdata[18] == 0x10)//hasta ahora error macrodcu
			{
				//manda siempre esta trama : 0000010005 ->empieza en posicion 11
				vecdlms.clear();

			}
		}
		m_typeframe = 0;
		break;
	}
	case 0xC4://event rf
	{
		vecdlms.clear();
		//mando todo el vector para ser parseado luego
		vecdlms.insert(vecdlms.begin(), vecdata.begin(), vecdata.end());
		m_frametype = 3;
		break;
	}
	default:
		break;
	}
	if (lenframeData > 12)//+48)
	{
		//**validat si el tp=1 osea si tiene marca de tiempo por ahora da por hecho que lo tiene
		//10 bytes = 1byte de cierre+1bytecheksum+6bytetime+2byteaux
		//48 bytes=6bytes header+1bytedomain+5bytesaddress-1bye+1byte+2byte+2byte
		//20 41 000 15
		//vecdlms.insert(vecdlms.begin(), vecdata.begin() + 48, vecdata.end() - 10);

	}
	else
	{

	}
	//68 32 00 32 00 68 C9 41 20 0F 00 00 02 71 00 00 04 00 B0 16
	//0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19
	//direccion de donde proviene el frame 7 + 5 bytes
	//41  20 0F 00 00 => 2041000015
	return vecdlms;
}


std::vector<unsigned char> Qgdw::StopReadingTask(std::string concentrator)
{
	//688E008E0068 4A 4120010004 04 E1 0000 0206 000000000000000000000000000000 0000 01022816010 0DF16 <- CON04050W
	//688E008E0068 4A 20211C0004 04 E2 0000 0206 000000000000000000000000000000 0000 023805161200 0016  
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comando1 = "000000000000000000000000000000";
	data1 = HexToBytes(comando1);
	crcdata1 = HexToBytes("0000");
	Vectotal = HexToBytes("4A") + FrameAddress(concentrator) + HexToBytes("04") + SEQ() + DA() + HexToBytes("0206") + data1 + AuxDataMP(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;

}

std::vector<unsigned char> Qgdw::ClearMeasurementPoint(std::string concentrator)
{
	//688E008E0068 4A 4120010004 05 E2 0000 1006 1F0000000000000000000000000000 0000 020728160100 1416 <- CON00107W
	//688E008E0068 4A 20211C0004 05 E1 0000 1006 1F0000000000000000000000000000 0000 013705161200 2B16 
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;

	std::string comando1 = "1F0000000000000000000000000000";
	data1 = HexToBytes(comando1);
	crcdata1 = HexToBytes("0000");
	Vectotal = HexToBytes("4A") + FrameAddress(concentrator) + HexToBytes("05") + SEQ() + DA() + HexToBytes("1006") + data1 + AuxDataMP(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;


}

std::vector<unsigned char> Qgdw::ClearTask(std::string concentrator)
{
	//688E008E0068 4A 20211C0004 04 E3 0000 8004 000000000000000000000000000000 0000 034508161200 8E16
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comando1 = "000000000000000000000000000000";
	data1 = HexToBytes(comando1);
	crcdata1 = HexToBytes("0000");
	Vectotal = HexToBytes("4A") + FrameAddress(concentrator) + HexToBytes("04") + SEQ() + DA() + HexToBytes("8004") + data1 + AuxDataMP(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::F33(std::string concentrator)
{
	//68D600D60068 4A 20211C0004 04 E4 0000 0104 011F0000FFFFFF7F15003C30230001150045230000000000000000000000000000 0000 045608161200 E016
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comando1 = "011F0000FFFFFF7F15003C30230001150045230000000000000000000000000000";
	data1 = HexToBytes(comando1);
	crcdata1 = HexToBytes("0000");
	Vectotal = HexToBytes("4A") + FrameAddress(concentrator) + HexToBytes("04") + SEQ() + DA() + HexToBytes("0104") + data1 + AuxDataMP(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::F38(std::string concentrator)
{
	//68FA00FA0068 4A 20211C0004 04 E5 0000 2004 010102180000000000000000000000000000000000000000000000FF0000000000000000000000000000 0000 050409161200 0D16
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comando1 = "010102180000000000000000000000000000000000000000000000FF0000000000000000000000000000";
	data1 = HexToBytes(comando1);
	crcdata1 = HexToBytes("0000");
	Vectotal = HexToBytes("4A") + FrameAddress(concentrator) + HexToBytes("04") + SEQ() + DA() + HexToBytes("2004") + data1 + AuxDataMP(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::F39(std::string concentrator)
{
	//681201120168 4A 20211C0004 04 E6 0000 4004 0101021E00000000000000000000000000000000000000000000000000000000FFFB0000000000000000000000000000 0000 061509161200 4116
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comando1 = "0101021E00000000000000000000000000000000000000000000000000000000FFFB0000000000000000000000000000";
	data1 = HexToBytes(comando1);
	crcdata1 = HexToBytes("0000");
	Vectotal = HexToBytes("4A") + FrameAddress(concentrator) + HexToBytes("04") + SEQ() + DA() + HexToBytes("4004") + data1 + AuxDataMP(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::F40(std::string concentrator)
{
	//686E026E0268 4A 20211C0004 04 E8 0000 8004 0402E20309026500080F00010300010402E3031D000012000012010000000601000000060402020102FF01626300000700010D02E4031D000012000012020000000602000000060402020102FF01626300000700010D02E5031D000012000012010000000601000000060402020102FF0002620000070001100000000000000000000000000000 0000 084909161200 1216
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comando1 = "0402E20309026500080F00010300010402E3031D000012000012010000000601000000060402020102FF01626300000700010D02E4031D000012000012020000000602000000060402020102FF01626300000700010D02E5031D000012000012010000000601000000060402020102FF0002620000070001100000000000000000000000000000";
	data1 = HexToBytes(comando1);
	crcdata1 = HexToBytes("0000");
	Vectotal = HexToBytes("4A") + FrameAddress(concentrator) + HexToBytes("04") + SEQ() + DA() + HexToBytes("8004") + data1 + AuxDataMP(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::ReadRfVersion(std::string concentrator)
{
	//689E009E0068 4B 20211C0004 10 E1 0000 0100 1FCB A814 0F00680F 000A000000000000030100 0E16 011910161200 4E16
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comand1 = "A8140F00680F";
	data1 = { 0x00,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x01,0x00 };
	crcdata1 = CheckSum(data1);
	Vectotal = HexToBytes("4B") + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + UNKOWN1() + HexToBytes(comand1) + data1 + AuxData(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::ReadNetworkParameter(std::string concentrator)
{
	//689E009E0068 4B 20211C0004 10 E2 0000 0100 1FCB A814 0F00680F 000A000000000000030200 0F16 022510161200 5E16
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comand1 = "A8140F00680F";
	data1 = { 0x00,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x02,0x00 };
	crcdata1 = CheckSum(data1);
	Vectotal = HexToBytes("4B") + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + UNKOWN1() + HexToBytes(comand1) + data1 + AuxData(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::SetNetworkParameter(std::string concentrator, int opc, int value)
{
	//frecuencia
	//68AA00AA0068 4B 20211C0004 10 E3 00000100 1FCB A814 12006812 000A0000000000000501 000698AD 5B16 034410161200 1D16 
	//canal
	//68A200A20068 4B 20211C0004 10 E4 00000100 1FCB A814 10006810 000A0000000000000502 0000 1116 040111161200 4516
	//netid
	//68A600A60068 4B 20211C0004 10 E1 00000100 1FCB A814 11006811 000A0000000000000504 000406 1D16 011611161200 6E16
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comand1 = "A814", values;
	switch (opc)
	{
	case 0://canal
	{
		comand1.append("10006810");
		data1 = { 0x00,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x02 };
		values = "00" + IntToHex(value);//4 bytes
		break;
	}
	case 1://netid
	{
		comand1.append("11006811");
		data1 = { 0x00,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x04 };
		values = "00" + IntToHex(value);//2 bytes
		break;
	}
	case 2://frecuencia
	{
		comand1.append("12006812");
		data1 = { 0x00,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x01 };
		values = "00" + IntToHex(value);//8 bytes
		break;
	}
	default:
		break;
	}
	data1 = data1 + HexToBytes(values);
	crcdata1 = CheckSum(data1);
	Vectotal = HexToBytes("4B") + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + UNKOWN1() + HexToBytes(comand1) + data1 + AuxData(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::AddNetPoint(std::string concentrator, std::string meters)
{
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::vector<std::string>vecmeters;
	Explode(meters, "-", &vecmeters);
	std::string invmeter, invmeters;
	//inversion del codigo de medidor
	for (size_t i = 0; i < vecmeters.size(); i++)
	{
		std::string meter1 = "0" + vecmeters.at(i);
		for (size_t j = 6; j >= 0; j--)
		{
			std::string tmp = meter1.substr(j * 2, 2);
			invmeter.append(tmp.c_str());
		}
		invmeters.append(invmeter);
		invmeter.clear();

	}
	int nummedidores = (int)vecmeters.size();
	int num1 = (nummedidores * 6) + 18;
	std::string lenmeters = "A814" + IntToHex((int)num1) + "0068" + IntToHex((int)num1);//numero de medidores que se van a procesar

	data1 = { 0x00,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x09,0x04,0x00,0x00,0x00 };// , 0x0F
	data1.push_back((unsigned char)nummedidores);
	//invmeters = "521535417203972035417203850835417203661935417203930835417203461435417203620535417203161035417203040635417203471335417203700535417203581935417203660135417203111535417203400235417203";
	HexToBytes(invmeters);
	data1 = data1 + HexToBytes(invmeters);
	crcdata1 = CheckSum(data1);
	//                                                     10             0000   0100    1FCB       A814                                  genrera crc con fecha
	Vectotal = DomainCtrl() + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + UNKOWN1() + HexToBytes(lenmeters) + data1 + AuxData(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::AuxDataMP(std::vector<unsigned char> checksumdata1)
{
	if (m_contaux1 > 255)
	{
		m_contaux1 = 0;
	}
	if (m_contaux2 > 255)
	{
		m_contaux2 = 0;
	}
	std::vector<unsigned char> vecaux;
	vecaux.insert(vecaux.end(), checksumdata1.begin(), checksumdata1.end());
	vecaux.push_back((unsigned char)m_contaux2);
	m_contaux1++;
	m_contaux2++;
	struct tm now = GetLocaltime();
	vecaux.push_back((unsigned char)HexToInt(std::to_string(now.tm_sec)));
	vecaux.push_back((unsigned char)HexToInt(std::to_string((now.tm_min))));
	vecaux.push_back((unsigned char)HexToInt(std::to_string(now.tm_hour)));
	vecaux.push_back((unsigned char)HexToInt(std::to_string(now.tm_mday)));
	vecaux.push_back(0x00);
	return vecaux;
}

std::vector<unsigned char> Qgdw::SetMeasurementPoint(std::string concentrator, std::string meters, int ini, int pending, int step)
{
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::vector<std::string>vecmeters, invvecmeters;
	Explode(meters, "-", &vecmeters);
	std::string invmeter;
	//inversion del codigo de medidor
	for (size_t i = 0; i < vecmeters.size(); i++)
	{
		std::string meter1 = "0" + vecmeters.at(i);
		for (size_t j = 6; j >= 0; j--)
		{
			std::string tmp0 = meter1.substr(j * 2, 2);
			invmeter.append(tmp0.c_str());
		}
		invvecmeters.push_back(invmeter);
		invmeter.clear();

	}

	//1400 02000200 DF14 107335417203 000000000000041700000000000012 02000200 DF14 337635417203 000000000000041700000000000012 02000200 DF14 12483541720300000000000004170000000000001203000300DF14 00753541720300000000000004170000000000001203000300DF14 14863641720300000000000004170000000000001203000300DF14 52663341720300000000000004170000000000001204000400DF14 94723541720300000000000004170000000000001204000400DF14 21873641720300000000000004170000000000001204000400DF14 45663341720300000000000004170000000000001205000500DF140 2733541720300000000000004170000000000001205000500DF14 80853641720300000000000004170000000000001205000500DF14 32613341720300000000000004170000000000001206000600DF14 69733541720300000000000004170000000000001206000600DF14 61633541720300000000000004170000000000001206000600DF141 1583341720300000000000004170000000000001207000700DF148 6723541720300000000000004170000000000001207000700DF144 0533341720300000000000004170000000000001207000700DF142 0653341720300000000000004170000000000001208000800DF1461713541720300000000000004170000000000001208000800DF14 06483341720300000000000004170000000000001200000000000000000000000000000000 015818153100 DF 16
	//                                                                    10              0000           0100          1FCB       A814                                  genrera crc con fecha
	int nummedidores = (int)vecmeters.size();
	std::string lenmeters = IntToHex((int)vecmeters.size()) + "00";//numero de medidores que se van a procesar
	std::string contadorproceso;// = IntToHex(cont) + "00" + IntToHex(cont) + "00";//contador de proceso
	std::string comando0 = "000000000000041700000000000012", comando1;
	size_t c1 = 0;// t1 = 2;t2=1 p1=2 globales
	int t1 = ini;
	int t2 = pending + 1;
	int p1 = step;
	while (c1 < (size_t)(nummedidores))//los bloques se generan de 20 en 20 medidores
	{
		if (t2 > p1)
		{
			t2 = 1;
			t1++;
		}
		t2++;
		contadorproceso = IntToHex(t1) + "00" + IntToHex(t1) + "00";//contador de proceso
		std::string tmp1 = contadorproceso + "DF14" + invvecmeters.at(c1) + comando0;
		comando1.append(tmp1);
		c1++;
	}
	//t2 = t2 - 1;
	data1 = HexToBytes(comando1);
	crcdata1 = HexToBytes("00000000000000000000000000000000");
	Vectotal = HexToBytes("4A") + FrameAddress(concentrator) + HexToBytes("04") + SEQ() + DA() + HexToBytes("0201") + HexToBytes(lenmeters) + data1 + AuxDataMP(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::AddBroadcastPointNumber(std::string concentrator, std::string meters)
{
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::vector<std::string>vecmeters;
	Explode(meters, "-", &vecmeters);
	std::string invmeter, invmeters;
	//inversion del codigo de medidor
	for (size_t i = 0; i < vecmeters.size(); i++)
	{
		std::string meter1 = "0" + vecmeters.at(i);
		for (size_t j = 6; j >= 0; j--)
		{
			std::string tmp = meter1.substr(j * 2, 2);
			invmeter.append(tmp.c_str());
		}
		invmeters.append(invmeter);
		invmeter.clear();

	}
	int nummedidores = (int)vecmeters.size();
	int num1 = (nummedidores * 6) + 23;
	std::string lenmeters = "BC14" + IntToHex((int)num1) + "0068" + IntToHex((int)num1);//numero de medidores que se van a procesar

	data1 = { 0x00,0x0A,0x01,0x00,0x00,0x00,0x00,0x00,0x05,0x80,0x00,0x00 };// , 0x0F
	data1.push_back((unsigned char)(nummedidores + 1));
	data1.push_back(0xFA); data1.push_back(0xFA); data1.push_back(0xFF); data1.push_back(0xFF); data1.push_back(0xFF); data1.push_back(0x07);
	HexToBytes(invmeters);
	data1 = data1 + HexToBytes(invmeters);
	crcdata1 = CheckSum(data1);
	//                                                     10             0000   0100    1FCB       A814                                  genrera crc con fecha
	Vectotal = DomainCtrl() + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + UNKOWN1() + HexToBytes(lenmeters) + data1 + AuxData(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::StartRfNetworking(std::string concentrator)
{
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comand1 = "A8140F00680F";
	//689E009E0068 4B 20211C0004 10 ED 0000 0100 1FCB A814 0F00680F 000A000000000000010400 0F16 0D5620161200 B5 16
	data1 = { 0x00,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x04,0x00 };
	crcdata1 = CheckSum(data1);
	//                                                     10      ED      0000   0100   1FCB        A814 0F00680F                                  genrera crc con fecha
	Vectotal = DomainCtrl() + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + UNKOWN1() + HexToBytes(comand1) + data1 + AuxData(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
	return std::vector<unsigned char>();
}

std::vector<unsigned char> Qgdw::ReadNodeLevel(std::string concentrator, int cont, int nummeters)
{
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	//68AA00AA0068 4B 20211C0004 10 EF 0000 0100 1FCB A814 12006812 000A0000000000001002 0001001D 3A16 0F4451161200 34 16
	std::string comand1 = "A81412006812";
	data1 = { 0x00,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x02,0x00 };
	int constante = 29;
	if (cont + 29 > nummeters)
	{
		constante = (nummeters - cont) + 1;
	}
	std::vector<unsigned char>lendata = HexToBytes(IntToHex((int)cont));
	if (lendata.size() == 1)
	{
		lendata.push_back(0x00);
	}
	else
	{
		std::reverse(lendata.begin(), lendata.end());

	}
	data1 = data1 + lendata;
	std::string l1;
	l1 = IntToHex(constante);
	data1 = data1 + HexToBytes(l1);
	crcdata1 = CheckSum(data1);
	Vectotal = HexToBytes("4B") + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + UNKOWN1() + HexToBytes(comand1) + data1 + AuxData(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::StopBuild(std::string concentrator)
{
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;
	std::string comand1 = "A8140F00680F";
	//689E009E0068 4B 20211C0004 10 E7 0000 0100 1FCB A814 0F00680F 000A000000000000010200 0D16 071157161200 97 16
	data1 = { 0x00,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x00 };
	crcdata1 = CheckSum(data1);
	//                                                     10      ED      0000   0100   1FCB        A814 0F00680F                                  genrera crc con fecha
	Vectotal = DomainCtrl() + FrameAddress(concentrator) + AFN() + SEQ() + DA() + DT() + UNKOWN1() + HexToBytes(comand1) + data1 + AuxData(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
	return std::vector<unsigned char>();

}

std::vector<unsigned char> Qgdw::StartTask(std::string concentrator)
{
	//688E008E0068 4A 20211C0004 04 E1 0000 0206 010000000000000000000000000000 0000 012457161200 3D16 <- CON04050W (212000028||1|31|0);
	std::vector<unsigned char> Vectotal;
	std::vector<unsigned char>tmp, data1, crcdata1;

	std::string comando1 = "010000000000000000000000000000";
	data1 = HexToBytes(comando1);
	crcdata1 = HexToBytes("0000");
	Vectotal = HexToBytes("4A") + FrameAddress(concentrator) + HexToBytes("05") + SEQ() + DA() + HexToBytes("0206") + data1 + AuxDataMP(crcdata1);
	tmp = HeaderFrame((int)Vectotal.size());
	Vectotal = Vectotal + CheckSum(Vectotal);
	Vectotal.insert(Vectotal.begin(), tmp.begin(), tmp.end());
	Vectotal.push_back(EndFrame());
	m_lenframe = (int)Vectotal.size();
	for (unsigned char i : Vectotal)
		printf("%.2X", i);
	printf("\n");
	return Vectotal;
}

std::vector<unsigned char> Qgdw::VerifyDC(std::string concentrator)
{
	concentrator = "";
	return std::vector<unsigned char>();
}

std::string Qgdw::ParseEvents(std::vector<unsigned char> data)
{
	std::string resp, fecha;
	if ((data[22] == 0x0E) && (data[23] == 0x0A)) //power outage,power back
	{
		//24 +5 bytes es la fecha y siguen EEEEEEE
		//29 +5 bytes es la fecha para powerback
		if (data[29] == 0xEE)//power outage
		{
			fecha = ByteToString(data.data(), 24, 5);
			fecha = "20" + fecha.substr(8, 2) + "-" + fecha.substr(6, 2) + "-" + fecha.substr(4, 2) + " " + fecha.substr(2, 2) + ":" + fecha.substr(0, 2);
			fecha = "1,1," + fecha;
			//5014210623  ->2023-06-21 14:50
		}
		else //powerback
		{
			fecha = ByteToString(data.data(), 29, 5);
			fecha = "20" + fecha.substr(8, 2) + "-" + fecha.substr(6, 2) + "-" + fecha.substr(4, 2) + " " + fecha.substr(2, 2) + ":" + fecha.substr(0, 2);
			fecha = "1,2," + fecha;
		}
		resp = fecha;
	}
	else if ((data[22] == 0x18) && (data[22] == 0x0E))
	{
		fecha = ByteToString(data.data(), 24, 5);
		fecha = "20" + fecha.substr(8, 2) + "-" + fecha.substr(6, 2) + "-" + fecha.substr(4, 2) + " " + fecha.substr(2, 2) + ":" + fecha.substr(0, 2);
		switch (data[31])
		{
		case 0x08://fase c is broken
		{
			fecha = "10,726," + fecha;
			break;
		}
		case 0x10://fase b is broken
		{
			fecha = "10,725," + fecha;
			break;
		}
		case 0x20://fase a is broken
		{
			fecha = "10,724," + fecha;
			break;
		}
		case 0x81://C phase occurrence voltage lower limit(767)
		{
			fecha = "10,767," + fecha;
			break;
		}
		case 0x82://B phase occurrence voltage lower limit(766)
		{
			fecha = "10,766," + fecha;
			break;
		}
		case 0x84://A phase occurrence voltage lower limit(765)
		{
			fecha = "10,765," + fecha;
			break;
		}
		default:
			fecha = "10,0," + fecha;//desconocido
			break;
		}
		resp = fecha;
	}
	else if ((data[22] == 0x21) && (data[23] == 0x0D))
	{
		fecha = ByteToString(data.data(), 24, 5);
		fecha = "20" + fecha.substr(8, 2) + "-" + fecha.substr(6, 2) + "-" + fecha.substr(4, 2) + " " + fecha.substr(2, 2) + ":" + fecha.substr(0, 2);
		//Power meter operation State Word modification(787)
		fecha = "10,787," + fecha;
		resp = fecha;
	}
	else//desconocido
	{
		resp = "-1,-1," + BytesToHex(data);
	}

	return resp;
}
