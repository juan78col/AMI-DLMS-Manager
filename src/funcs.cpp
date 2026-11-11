#include "funcs.h"
#include <guruxdlms/GXHelpers.h>
#include <sys/stat.h>
#include <cstring>
#ifdef _WIN32
#include <direct.h>
#define GetCurrentDir _getcwd
#else
#include <unistd.h>
#define GetCurrentDir getcwd
#endif
#include <ranges>
std::string GetCurrentWorkingDirectory()
{   
    char buff[FILENAME_MAX];
    GetCurrentDir(buff, FILENAME_MAX);
    return std::string(buff);
}
tm GetLocaltime()
{
    std::time_t currentTime = std::time(nullptr);
    std::tm timeInfo = {};
#ifdef _WIN32
    localtime_s(&timeInfo, &currentTime);  // En Windows
#else
    localtime_r(&currentTime, &timeInfo);  // En sistemas no Windows
#endif
    return timeInfo;
}
uint16_t crc16_x25(const unsigned char* pData, int length)
{
    int i;
    uint16_t wCrc = 0xffff;
    while (length--) {
        wCrc ^= *(unsigned char*)pData++ << 0;
        for (i = 0; i < 8; i++)
            wCrc = wCrc & 0x0001 ? ((wCrc >> 1) ^ 0x8408) :( wCrc >> 1);
    }
    return wCrc ^ 0xffff;
}

std::string Implode(std::string delimiter, const std::vector<std::string>& arr)
{
    std::string res;
    for (size_t i = 0; i < arr.size(); ++i)
        res += arr[i] + delimiter;

    res = res.substr(0, res.length() - 1); //more efficient than checking inside the loop
    return res;
}

std::vector<std::string> Split(const std::string& cadena, char delimitador)
{
	std::vector<std::string> resultado;
	Split(cadena, delimitador, &resultado);
	return resultado;
}

void Split(const std::string& cadena, char delimitador, std::vector<std::string>* resultado)
{
	if (!resultado) return;
    resultado->clear();
    resultado->reserve(1 + static_cast<size_t>(std::count(cadena.begin(), cadena.end(), delimitador)));
    for (auto&& token : std::views::split(cadena, delimitador)) {
        // Construye std::string desde el rango [begin, end)
        resultado->emplace_back(token.begin(), token.end());
    }
}

void SplitInt(std::string cadena, std::string delimitador, std::vector<int>* resultado)
{
    size_t found;
    found = cadena.find_first_of(delimitador);
    while (found != std::string::npos) {
        if (found > 0) {
            resultado->push_back(atoi(cadena.substr(0, found).c_str()));
        }
        cadena = cadena.substr(found + 1);
        found = cadena.find_first_of(delimitador);
    }
    if (cadena.length() > 0) {
        resultado->push_back(atoi(cadena.c_str()));
    }
}

int FindDateTime(const std::vector<std::string>& fechas, const CGXDateTime& buscar)
{
	CGXDateTime datetmp;
	auto target = buscar;

	for (size_t i = 0; i < fechas.size(); i++)
    {
        struct tm tm {};
        StringToDate(fechas[i], "%Y-%m-%d %H:%M:%S", tm);
        if ((tm.tm_hour == target.GetValue().tm_hour) && (tm.tm_min == target.GetValue().tm_min))
        {
            return (int)i;
            break;
        }
    }
    return -1;
}

void StringToDate(std::string fecha, std::string format, tm &m_tm)
{
    strptime(fecha.c_str(), format.c_str(), &m_tm);
}

std::string DateToString(std::string format, tm m_tm)
{
    char buffer[80];
    strftime(buffer, sizeof(buffer),format.c_str(), &m_tm);
    std::string resp(buffer);
    return resp;
}

int CompareDateTimeNow(std::vector<std::string> fechas)
{
    //struct tm* now = NULL;
    //time_t time_value = 0;
    //time_value = time(NULL);
    //now = localtime(&time_value);
    struct tm now = GetLocaltime();

    std::vector<std::string>tmp = fechas;
    struct tm tm;
    for (size_t i = 0; i < tmp.size(); i++)
    {
        StringToDate(tmp.at(i), "%Y-%m-%d %H:%M:%S", tm);
        if ((tm.tm_hour == now.tm_hour) && (tm.tm_min == now.tm_min))
        {
            return (int)i;
            break;
        }
    }
    return -1;
}

std::string DateTimeNow()
{
    //time_t time_value = time(NULL);
    //struct tm* now = NULL;
    //now = localtime(&time_value);
    struct tm now = GetLocaltime();
    char buffer[30];
    
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &now);
    std::string fecha = buffer;
    return fecha;
}
std::string AddMinuteToDate(std::string date, int minutes)
{
    struct tm tm;
    StringToDate(date, "%Y-%m-%d %H:%M:%S", tm);
    //tm.tm_min += minutes;
    CGXDateTime dt;
    dt.SetValue(tm);
    dt.AddMinutes(minutes);
    //mktime(&tm);
    return DateToString("%Y-%m-%d %H:%M:%S", dt.GetValue());
}
std::string ByteToString(unsigned char bTemp[], int startLen, int endLen)
{
    std::string str = "";
    char tmpstr[256];
    for (int i = 0; i < endLen; i++)
    {
        sprintf(tmpstr, "%02X", bTemp[i + startLen]);
        str.append(tmpstr);
    }

    return str;

}

std::string ExecuteCommandLine(std::string command) 
{
    char buffer[128];
    std::string result = "";

    // Open pipe to file
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "popen failed!";
    }

    // read till end of process:
    while (!feof(pipe)) {

        // use buffer to read and add to result
        if (fgets(buffer, 128, pipe) != NULL)
            result += buffer;
    }

    pclose(pipe);
    return result;
}

void WriteLog(std::string stringlog,int server)
{
    //struct tm* now = NULL;
    //time_t time_value = 0;
    //time_value = time(NULL);
    //now = localtime(&time_value);
    struct tm now = GetLocaltime();
    std::vector<std::string>dias = {"Dom","Lun","Mar","Mie","Jue","Vie","Sab"};
    std::string archivo;
    archivo = "srv-" + std::to_string(server) + "-trace-" + dias[(size_t)now.tm_wday] + ".txt";
    struct stat result;
    if (stat(archivo.c_str(), &result) == 0)
    {
        time_t tiempo = time(0);
        time_t timefile = result.st_mtime;
        double diferencia=difftime(tiempo, timefile);
        if (diferencia>86400)
        {
            remove(archivo.c_str());
        }
        
    }
    GXHelpers::Write(archivo, stringlog);
    
}

std::string GetMeterInhfromPulse(unsigned char bTemp[])
{
    //unsigned char demo[13] = { 0x0D,0x00,0x29,0x75,0x20,0x29,0x72,0x03,0x00,0x00,0x00,0xD4,0x01 };
    std::string bytemeter = ByteToString(bTemp, 2, 6);
    std::string meter;
    for (size_t i = 6; i >= 0; i--)
    {
        meter.append(bytemeter.substr(i * 2, 2).c_str());
    }
    return meter;
}
/*
* obtiene id del medidor enviado en el pulso
* btemp bytes enviados por medidor
* type tipo de medidor escojido
* 0 inh,1 inh macro,2 zstar,3 macro szstar,4 microstar

*/
std::string GetIdMeterMeterfromPulse(unsigned char bTemp[], int type)
{
    std::string meter;
    switch (type)
    {
    case 0://inh meter
    {
        std::string bytemeter = ByteToString(bTemp, 2, 6);
        for (size_t i = 6; i >= 0; i--)
        {
            meter.append(bytemeter.substr(i * 2, 2).c_str());
        }
        break;
    }
    case 1://inh macrometer
    {
        break;
    }
    case 2://star meter
    {
        break;
    }
    case 3://star macrometer
    {
        break;
    }
    case 4://microstar meter
    {
        //std::string bytemeter = ByteToString(bTemp, 34, 8);
        //3231313233303832
        for (int i = 0; i <= 7; i++)
        {
            //meter.append(bytemeter.substr(i * 2, 2).c_str());
            meter += static_cast<char>(bTemp[34+i]);
            
        }
        //meter = bytemeter;
        break;
    }
    default:
        break;
    }
    return meter;
}

std::string GetIdConcentratorfromPulse(unsigned char bTemp[], int type)
{
    std::string dcuid;
    switch (type)
    {
        //00 01 00 01 00 01 00 0F 0A 01 09 30 30 35 37 30 30 39 30 34 05 10 B7
    case 0://concentrador star
    {
        for (size_t i = 0; i < 9; i++)
        {
            dcuid.append(1,(char)bTemp[11 + i]);

        }
        //dcuid = ByteToString(bTemp, 11, 9);
        break;
    }
    default:
        break;
    }
    return dcuid;
}

std::string GetDateTimeFromBuffer(CGXByteBuffer buffer)
{
    DATETIME_SKIPS skip = DATETIME_SKIPS_NONE;
    struct tm tm = {  };
    unsigned short year;
    short deviation;
    int ret, ms, status;
    unsigned char ch;
    // If there is not enough data available.
    if (buffer.GetSize() - buffer.GetPosition() < 12)
    {
        return "";
    }
    // Get year.
    if ((ret = buffer.GetUInt16(&year)) != 0)
    {
        return "";
    }
    tm.tm_year = year;
    // Get month
    if ((ret = buffer.GetUInt8(&ch)) != 0)
    {
        return "";
    }
    tm.tm_mon = ch;
    // Get day
    if ((ret = buffer.GetUInt8(&ch)) != 0)
    {
        return "";
    }
    tm.tm_mday = ch;
    // Skip week day
    if ((ret = buffer.GetUInt8(&ch)) != 0)
    {
        return "";
    }
    tm.tm_wday = ch;
    // Get time.
    if ((ret = buffer.GetUInt8(&ch)) != 0)
    {
       return "";
    }
    tm.tm_hour = ch;
    if ((ret = buffer.GetUInt8(&ch)) != 0)
    {
        return "";
    }
    tm.tm_min = ch;
    if ((ret = buffer.GetUInt8(&ch)) != 0)
    {
        return "";
    }
    tm.tm_sec = ch;
    if ((ret = buffer.GetUInt8(&ch)) != 0)
    {
       return "";
    }
    ms = ch;
    if (ms != 0xFF)
    {
        ms *= 10;
    }
    else
    {
        skip = (DATETIME_SKIPS)(skip | DATETIME_SKIPS_MS);
        ms = 0;
    }
    if ((ret = buffer.GetInt16(&deviation)) != 0)
    {
        return "";
    }
    //0x8000 == -32768
    if ((ret = buffer.GetUInt8(&ch)) != 0)
    {
        return "";
    }
    status = ch;
    CGXDateTime dt;
    if (status == 0xFF)
    {
        status = 0;
        skip = (DATETIME_SKIPS)(skip | DATETIME_SKIPS_STATUS);
    }
    dt.SetStatus((DLMS_CLOCK_STATUS)status);
    if (year < 1 || year == 0xFFFF)
    {
        skip = (DATETIME_SKIPS)(skip | DATETIME_SKIPS_YEAR);
        tm.tm_year = 0;
    }
    else
    {
        tm.tm_year -= 1900;
    }
    if (tm.tm_wday < 0 || tm.tm_wday > 7)
    {
        tm.tm_wday = 0;
        skip = (DATETIME_SKIPS)(skip | DATETIME_SKIPS_DAYOFWEEK);
    }
    if (tm.tm_mon == 0xFE)
    {
        //Daylight savings begin.
        tm.tm_mon = 0;
        dt.SetExtra((DATE_TIME_EXTRA_INFO)(dt.GetExtra() | DATE_TIME_EXTRA_INFO_DST_BEGIN));
    }
    else if (tm.tm_mon == 0xFD)
    {
        // Daylight savings end.
        tm.tm_mon = 0;
        dt.SetExtra((DATE_TIME_EXTRA_INFO)(dt.GetExtra() | DATE_TIME_EXTRA_INFO_DST_END));
    }
    else if (tm.tm_mon < 1 || tm.tm_mon > 12)
    {
       skip = (DATETIME_SKIPS)(skip | DATETIME_SKIPS_MONTH);
       tm.tm_mon = 0;
    }
    else
    {
        tm.tm_mon -= 1;
    }
    if (tm.tm_mday == 0xFD)
    {
        // 2nd last day of month.
        tm.tm_mday = 1;
        dt.SetExtra((DATE_TIME_EXTRA_INFO)(dt.GetExtra() | DATE_TIME_EXTRA_INFO_LAST_DAY2));
    }
    else if (tm.tm_mday == 0xFE)
        {
            //Last day of month
            tm.tm_mday = 1;
            dt.SetExtra((DATE_TIME_EXTRA_INFO)(dt.GetExtra() | DATE_TIME_EXTRA_INFO_LAST_DAY));
        }
    else if (tm.tm_mday < 1 || tm.tm_mday > 31)
        {
            skip = (DATETIME_SKIPS)(skip | DATETIME_SKIPS_DAY);
            tm.tm_mday = 1;
        }
    if (tm.tm_hour < 0 || tm.tm_hour > 24)
        {
            skip = (DATETIME_SKIPS)(skip | DATETIME_SKIPS_HOUR);
            tm.tm_hour = 0;
        }
    if (tm.tm_min < 0 || tm.tm_min > 60)
        {
            skip = (DATETIME_SKIPS)(skip | DATETIME_SKIPS_MINUTE);
            tm.tm_min = 0;
        }
    if (tm.tm_sec < 0 || tm.tm_sec > 60)
        {
            skip = (DATETIME_SKIPS)(skip | DATETIME_SKIPS_SECOND);
            tm.tm_sec = 0;
        }
    // If ms is Zero it's skipped.
    if (ms < 1 || ms > 1000)
        {
            ms = 0;
        }
    tm.tm_isdst = (status & DLMS_CLOCK_STATUS_DAYLIGHT_SAVE_ACTIVE) != 0;
    dt.SetValue(tm);
    dt.SetDeviation(deviation);
    dt.SetSkip(skip);
    return DateToString("%Y-%m-%d %H:%M:%S", tm);
}

std::vector<unsigned char> HexToBytes(const std::string& hex) {
    std::vector<unsigned char> bytes;

    for (unsigned int i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        unsigned char byte = (unsigned char)strtol(byteString.c_str(), NULL, 16);
        bytes.push_back(byte);
    }

    return bytes;
}

[[nodiscard]] std::string IntToHex(int value, std::string_view suffix)
{
    const bool negative = value < 0;
    std::uint32_t u = static_cast<std::uint32_t>(value);

    char tmp[10];
    char* p = tmp + sizeof(tmp);
    constexpr char digits[] = "0123456789ABCDEF";
    do { *--p = digits[u & 0xF]; u >>= 4; } while (u);

    std::size_t len = static_cast<std::size_t>(tmp + sizeof(tmp) - p);
    const bool needPad = (len & 1u) != 0;

    std::string r;
    r.reserve(len + (negative ? 1 : 0) + (needPad ? 1 : 0) + suffix.size());
    if (negative) r.push_back('-');
    if (needPad)  r.push_back('0');
    r.append(p, len);
    r.append(suffix);
    return r;
}

std::string BytesToHex(std::vector<unsigned char> data)
{
    std::string str = "", tr = "";
    size_t endLen = data.size();
    size_t startLen = 0;
    for (size_t i = 0; i < endLen; i++)
    {
        unsigned char tmp = data[i + startLen];
        tr = IntToHex((int)tmp);
        str = str + IntToHex((int)tmp);
    }
    return str;
}

int HexToInt(std::string value)
{
    //unsigned int x = (unsigned int)std::stoll(value, nullptr, 16);
    
    unsigned int x = (unsigned int)std::stoul(value, nullptr, 16);
    return (int)x;
}

std::string ltrim(std::string_view s)
{
    auto first = s.begin();
    auto last = s.end();
    while (first != last && std::isspace(static_cast<unsigned char>(*first))) ++first;
    return std::string(first, last);
}

std::string rtrim(std::string_view s)
{
    auto first = s.begin();
    auto last = s.end();
    while (first != last && std::isspace(static_cast<unsigned char>(*(last - 1)))) --last;
    return std::string(first, last);
}

std::string trim(std::string_view s)
{
    auto first = s.begin();
    auto last = s.end();
    while (first != last && std::isspace(static_cast<unsigned char>(*first))) ++first;
    while (first != last && std::isspace(static_cast<unsigned char>(*(last - 1)))) --last;
    return std::string(first, last);
}

std::string lpad(std::string_view cadena, std::size_t count, char padchar)
{
    if (count <= cadena.size()) return std::string(cadena);
    std::string cad;
    cad.reserve(count);
    cad.assign(count - cadena.size(), padchar);
    cad.append(cadena);
    return cad;
}
