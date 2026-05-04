#pragma once
#include <vector>
#include<string>
#include <cstdint>
#if _DEBUG_
#include "guruxdlms/GXDateTime.h"
#include "guruxdlms/GXBytebuffer.h"
#else
//#include "guruxdlms2/include/GXDateTime.h"
//#include "guruxdlms2/include/GXBytebuffer.h"
#include "guruxdlms/GXDateTime.h"
#include "guruxdlms/GXBytebuffer.h"

#endif // _DEBUG_

#include <algorithm> 
#include <unordered_map>
std::string GetCurrentWorkingDirectory();
tm GetLocaltime();
uint16_t crc16_x25(const unsigned char* pData, int length);
//uint16_t crc16_x25(const uint8_t* pData, size_t length);
std::string Implode(std::string delimiter, const std::vector<std::string>& arr);
void Explode(std::string cadena, std::string delimitador, std::vector<std::string>* resultado);
void ExplodeInt(std::string cadena, std::string delimitador, std::vector<int>* resultado);
//void ExplodeDateTime(std::string cadena, std::string delimitador, std::vector<CGXDateTime>* resultado);
int FindDateTime(std::vector<std::string> fechas, CGXDateTime buscar);
void StringToDate(std::string fecha, std::string format, tm &m_tm);
std::string DateToString(std::string format, tm m_tm);
/// <summary>
/// compara el rango de fechas del vector con la fecha actual del sistema
/// </summary>
/// <param name="fechas"></param>
/// <returns></returns>
int CompareDateTimeNow(std::vector<std::string> fechas);
std::string DateTimeNow();
std::string AddMinuteToDate(std::string date, int minutes);
std::string ByteToString(unsigned char bTemp[], int startLen, int endLen);
std::string ExecuteCommandLine(std::string command);
void WriteLog(std::string stringlog,int server=1);
std::string GetMeterInhfromPulse(unsigned char bTemp[]);
std::string GetIdMeterMeterfromPulse(unsigned char bTemp[], int type);
std::string GetIdConcentratorfromPulse(unsigned char bTemp[], int type);
std::string ltrim(std::string s);
std::string rtrim(std::string s);
std::string trim(std::string s);
std::string GetDateTimeFromBuffer(CGXByteBuffer buffer);
std::vector<unsigned char> HexToBytes(const std::string& hex);
std::string IntToHex(int value);
std::string BytesToHex(std::vector<unsigned char> data);
int HexToInt(std::string value);
std::string lpad(std::string cadena, size_t count, char padchar = '0');
void Removechars(char str[], int indexBegin, int indexEnd);
DLMS_CONFORMANCE StringToDLMSConformance(const std::string& str);
template<typename EnumType>
inline EnumType StringToEnum(const std::unordered_map<std::string, EnumType>& enumMap, const std::string& str)
{
	EnumType result = static_cast<EnumType>(0); // Inicializa con un valor por defecto
	std::istringstream iss(str);
	std::string s;
	while (std::getline(iss, s, '|'))
	{
		auto it = enumMap.find(trim(s)); 
        if (it != enumMap.end()) 
        { 
            result = static_cast<EnumType>(result | it->second); 
        }
	}
	return result;
}
static inline void Now2(std::string& str)
{
    struct tm dt = GetLocaltime();
    char tmp[21];
    int ret;
#if _MSC_VER > 1000
    ret = sprintf_s(tmp, 10, "%.2d:%.2d:%.2d", dt.tm_hour, dt.tm_min, dt.tm_sec);
#else
    ret = sprintf(tmp, "%d-%.2d-%.2d %.2d:%.2d:%.2d", (dt.tm_year + 1900), (dt.tm_mon + 1), dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
    //ret = sprintf(tmp, "%.2d:%.2d:%.2d", dt.tm_hour, dt.tm_min, dt.tm_sec);
#endif
    str.append(tmp, (size_t)ret);
}
struct MeterConfig {
    int Id;
    bool logicalNameReferencing;
    int clientAddress;
    int serverAddress;
    DLMS_AUTHENTICATION authentication;
    std::string password;
    DLMS_INTERFACE_TYPE interfaceType;
    std::string /*DLMS_CONFORMANCE*/ proposedConformance;
    std::string /*DLMS_CONFORMANCE*/ negotiatedConformance;
    std::string /*DLMS_SERVICE_CLASS*/ serviceClass;
    int maxreceivepdusize;
    std::string datetimeskips;
    int autoincreaseinvokeid;
    std::string security;
    std::string securitysuite;
    int meterType;
};
