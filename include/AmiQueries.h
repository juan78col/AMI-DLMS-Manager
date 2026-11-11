#pragma once
namespace AmiQueries {
    std::string OBTENER_COMANDOS = "CALL ami_core.obtener_comandos_pendientes();";
    std::string REGISTRAR_LECTURA = "CALL ami_core.insertar_lectura($1, $2);";
    std::string ACTUALIZAR_ESTADO = "CALL ami_core.actualizar_comando($1, $2, $3);";
    std::string CALL_MONITOR = "select * from func_monitor($1::varchar[])";
    std::string UPDATEMETERSTATUS = "select * from updatemeterstatus($1, $2)";
    std::string SAVEPROFILEDATA = "select * from saveprofiledata($1::varchar[])";
    std::string UPDATECOMMANDSTATUS = "select * from updatecommandstate($1, $2)";
    std::string SAVEMETERDATA = "select * from savemeterdata($1::varchar[])";
    std::string SAVEMETERDATABULK = "select * from savemeterdatabulk($1::varchar[][],$2)";
    std::string SAVEMETERRESPONSE = "select * from savemeterresponse($1::varchar[])";
    std::string SAVEMETERREQUEST = "select * from savemeterrequest($1::varchar[])";
    std::string GETPROFILEVI = "select * from getprofilevi($1)";
    std::string UPDATEPROFILE = "select * from updateprofile($1::varchar[])";
    std::string SAVEDCUEVENTINH = "select * from savedcueventinh($1::varchar[])";
    std::string GETCOMMANDDCU = "select * from getcommanddcu($1::varchar,3)";
    std::string GETPARAMMETER = "select * from getparammeter($1::varchar)";
	std::string FINDDEVICEBYIP = "select * from finddevicebyip($1::varchar,$2::integer)";
	std::string LOADALLPROFILES = "select * from loadallprofiles()";
}
