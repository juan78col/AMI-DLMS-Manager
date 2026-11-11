// dlms_profile_service.cpp
#include "dlms_profile_service.h"
#include "db_row.h"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <vector>

// -----------------------------------------------------------------------------
// Columnas en el MISMO orden que el SELECT de kProfilesQuery
// -----------------------------------------------------------------------------
// dlms_profile_service.cpp

static const std::vector<std::string> kProfilesColumns = {
    "id", "typeclcosem",
    "use_logical_name", "interface_type", "client_address", "server_address",
    "use_server_helper", "helper_logical", "helper_physical", "helper_address",
    "authentication", "password_source", "password_literal",
    "service_class", "max_receive_pdu", "security",
    "has_hdlc", "hdlc_window_tx", "hdlc_window_rx", "hdlc_maxinfo_tx", "hdlc_maxinfo_rx",
    "set_auto_increase", "auto_increase_invokeid",
    "meter_type", "type_interface",
    "security_suite", "security_level",
    "proposed_mask", "negotiated_mask", "datetime_skips_mask"
};

static const std::vector<std::string> kConnectionDataColumns = {
    "profile_id", "system_title_cifrado", "cipher_key_cifrada",
    "authentication_key_cifrada", "dedicated_key_cifrada"
};

static const char* kProfilesQuery = R"SQL(
SELECT
    p.id::text, p.typeclcosem::text,
    p.use_logical_name::text, p.interface_type::text,
    p.client_address::text, p.server_address::text,
    p.use_server_helper::text,
    COALESCE(p.helper_logical::text,''), COALESCE(p.helper_physical::text,''),
    COALESCE(p.helper_address::text,''),
    p.authentication::text, p.password_source,
    COALESCE(p.password_literal,''),
    p.service_class::text, p.max_receive_pdu::text, p.security::text,
    p.has_hdlc::text,
    COALESCE(p.hdlc_window_tx::text,''), COALESCE(p.hdlc_window_rx::text,''),
    COALESCE(p.hdlc_maxinfo_tx::text,''), COALESCE(p.hdlc_maxinfo_rx::text,''),
    p.set_auto_increase::text, p.auto_increase_invokeid::text,
    p.meter_type::text, p.type_interface::text,
    p.security_suite::text, p.security_level::text, -- <-- AÑADIDOS
    COALESCE((SELECT bit_or(bit_value) FROM dlms_profile_conformance
              WHERE profile_id = p.id AND kind = 'proposed'),   0)::text,
    COALESCE((SELECT bit_or(bit_value) FROM dlms_profile_conformance
              WHERE profile_id = p.id AND kind = 'negotiated'), 0)::text,
    COALESCE((SELECT bit_or(bit_value) FROM dlms_profile_datetime_skips
              WHERE profile_id = p.id), 0)::text
FROM dlms_profiles p
WHERE p.enabled = TRUE
ORDER BY p.id
)SQL";

/*static const char* kResolveMeterQuery = R"SQL(
SELECT COALESCE(profile_id, 0)::text
FROM v_meter_effective_profile
WHERE meter_serial = $1
)SQL";*/

static const char* kResolveMeterQuery = R"SQL(
SELECT
    profile_id::text,
    COALESCE(system_title_cifrado,''),
    COALESCE(cipher_key_cifrada,''),
    COALESCE(authentication_key_cifrada,''),
    COALESCE(dedicated_key_cifrada,'')
FROM dlms.get_meter_connection_data($1)
)SQL";

static const char* kResolveDcuQuery = R"SQL(
SELECT
    profile_id::text,
    COALESCE(system_title_cifrado,''),
    COALESCE(cipher_key_cifrada,''),
    COALESCE(authentication_key_cifrada,''),
    COALESCE(dedicated_key_cifrada,'')
FROM dlms.get_dcu_connection_data($1)
)SQL";


// -----------------------------------------------------------------------------
// RowToProfile: DbRow (por nombre) → struct
// -----------------------------------------------------------------------------
static bool RowToProfile(const DbRow& r, DlmsClientProfile& p)
{
    try {
        p.id = r.i("id");
        p.typeclcosem = r.i("typeclcosem");

        p.useLogicalName = r.b("use_logical_name");
        p.interfaceType = static_cast<uint32_t>(r.i("interface_type"));
        p.clientAddress = static_cast<uint32_t>(r.i("client_address"));
        p.serverAddress = static_cast<uint32_t>(r.i("server_address"));
        p.useServerAddrHelper = r.b("use_server_helper");
        if (p.useServerAddrHelper) {
            p.helperLogical = static_cast<uint32_t>(r.i("helper_logical"));
            p.helperPhysical = static_cast<uint32_t>(r.i("helper_physical"));
            p.helperAddress = static_cast<uint32_t>(r.i("helper_address"));
        }

        p.authentication = static_cast<uint32_t>(r.i("authentication"));
        const std::string src = r.s("password_source");
        if (src == "device") {
            p.useDevicePassword = true;
        }
        else if (src == "literal") {
            p.useDevicePassword = false;
            p.passwordLiteral = r.s("password_literal");
        }
        else {
            p.useDevicePassword = false;
            p.passwordLiteral.clear();
        }

        p.proposedConformance = r.u64("proposed_mask");
        p.negotiatedConformance = r.u64("negotiated_mask");
        p.hasProposed = p.proposedConformance != 0;
        p.hasNegotiated = p.negotiatedConformance != 0;

        p.serviceClass = static_cast<uint32_t>(r.i("service_class"));
        p.maxReceivePduSize = static_cast<uint32_t>(r.i("max_receive_pdu"));
        p.security = static_cast<uint32_t>(r.i("security"));

        p.dateTimeSkips = r.u64("datetime_skips_mask");
        p.hasDateTimeSkips = p.dateTimeSkips != 0;

        p.hasHdlc = r.b("has_hdlc");
        if (p.hasHdlc) {
            p.hdlcWindowTx = static_cast<uint32_t>(r.i("hdlc_window_tx"));
            p.hdlcWindowRx = static_cast<uint32_t>(r.i("hdlc_window_rx"));
            p.hdlcMaxInfoTx = static_cast<uint32_t>(r.i("hdlc_maxinfo_tx"));
            p.hdlcMaxInfoRx = static_cast<uint32_t>(r.i("hdlc_maxinfo_rx"));
        }

        p.setAutoIncreaseInvokeId = r.b("set_auto_increase");
        p.autoIncreaseInvokeId = r.b("auto_increase_invokeid");

        p.meterType = r.i("meter_type");
        p.typeInterface = r.i("type_interface");

		p.securitySuite = static_cast<uint32_t>(r.i("security_suite"));
		p.securityLevel = static_cast<uint32_t>(r.i("security_level"));

		if (p.clientAddress == 0 && p.serverAddress == 0)
            return false;

        return true;
    }
    catch (const std::exception&) {
        return false;
    }
}

// -----------------------------------------------------------------------------
// Singleton
// -----------------------------------------------------------------------------
static std::unique_ptr<DlmsProfileService> g_instance;
static std::once_flag                      g_once;

DlmsProfileService& DlmsProfileService::Instance()
{
    if (!g_instance)
        throw std::runtime_error("DlmsProfileService not initialized. Call Init() first.");
    return *g_instance;
}

void DlmsProfileService::Init(ConnProvider provider)
{
    std::call_once(g_once, [&]() {
        g_instance.reset(new DlmsProfileService(std::move(provider)));
        });
}

DlmsProfileService::DlmsProfileService(ConnProvider provider)
    : m_provider(std::move(provider)) {}

// -----------------------------------------------------------------------------
// Carga completa (async). Construye fuera del lock; publica atómico al final.
// -----------------------------------------------------------------------------
boost::asio::awaitable<bool> DlmsProfileService::LoadAllFromDb()
{
    std::unordered_map<int, DlmsClientProfile> freshPk;
    std::unordered_map<int, int>               freshType;

    try {
        auto conn = co_await m_provider();
        auto rs = co_await conn->execute_query(kProfilesQuery, {});

        freshPk.reserve(rs.countRows());
        for (size_t r = 0; r < rs.countRows(); ++r) {
            DbRow row(rs, r, kProfilesColumns);
            DlmsClientProfile p;
            if (!RowToProfile(row, p)) {
                spdlog::error("Skipping malformed DLMS profile id={}", row.i("id"));
                continue;
            }
            if (p.typeclcosem > 0)
                freshType[p.typeclcosem] = p.id;
            freshPk.emplace(p.id, std::move(p));
        }
    }
    catch (const std::exception& e) {
        spdlog::error("DLMS profiles load failed: {}. Keeping previous cache.", e.what());
        co_return false;
    }

    if (freshPk.empty()) {
        spdlog::error("DLMS profiles load returned 0 rows. Keeping previous cache.");
        co_return false;
    }

    {
        std::unique_lock lk(m_mutex);
        m_byPk = std::move(freshPk);
        m_byType = std::move(freshType);
        m_generation++;
        spdlog::info("DLMS profiles reloaded: {} profiles, gen={}",
            m_byPk.size(), m_generation);
    }
    co_return true;
}

boost::asio::awaitable<bool> DlmsProfileService::Reload()
{
    return LoadAllFromDb();
}

// -----------------------------------------------------------------------------
// Lectura de caché (sync, thread-safe)
// -----------------------------------------------------------------------------
bool DlmsProfileService::GetByPk(int pk, DlmsClientProfile& out) const
{
    std::shared_lock lk(m_mutex);
    auto it = m_byPk.find(pk);
    if (it == m_byPk.end()) return false;
    out = it->second;
    return true;
}

bool DlmsProfileService::GetByTypeclcosem(int typeclcosem, DlmsClientProfile& out) const
{
    std::shared_lock lk(m_mutex);
    auto itT = m_byType.find(typeclcosem);
    if (itT == m_byType.end()) return false;
    auto it = m_byPk.find(itT->second);
    if (it == m_byPk.end()) return false;
    out = it->second;
    return true;
}

uint64_t DlmsProfileService::Generation() const
{
    std::shared_lock lk(m_mutex);
    return m_generation;
}

size_t DlmsProfileService::CountProfiles() const
{
    std::shared_lock lk(m_mutex);
    return m_byPk.size();
}

// -----------------------------------------------------------------------------
// Resolución medidor → perfil (async: 1 SELECT + caché)
// -----------------------------------------------------------------------------
boost::asio::awaitable<bool> DlmsProfileService::ResolveForMeter(
    const std::string& meterSerial, DlmsClientProfile& out)
{
    int profileId = 0;
    std::string systemTitleCifrado;
    std::string cipherKeyCifrada;
    std::string authenticationKeyCifrada;
    std::string dedicatedKeyCifrada;
    try {
        auto conn = co_await m_provider();
        auto rs = co_await conn->execute_query(kResolveMeterQuery, { meterSerial });
        if (rs.countRows() > 0) {
            DbRow row(rs, 0, kConnectionDataColumns);
            profileId = row.i("profile_id");
            systemTitleCifrado = row.s("system_title_cifrado");
            cipherKeyCifrada = row.s("cipher_key_cifrada");
            authenticationKeyCifrada = row.s("authentication_key_cifrada");
            dedicatedKeyCifrada = row.s("dedicated_key_cifrada");
        }
    }
    catch (const std::exception& e) {
        spdlog::error("ResolveForMeter({}) db error: {}", meterSerial, e.what());
        co_return false;
    }

    if (profileId <= 0) {
        spdlog::error("Meter {} has no effective DLMS profile (neither meter nor DCU)",
            meterSerial);
        co_return false;
    }

    if (!GetByPk(profileId, out)) {
        spdlog::error("Meter {} resolved to profile id={} but not in cache (stale?)",
            meterSerial, profileId);
        co_return false;
    }

    out.systemTitleCifrado = std::move(systemTitleCifrado);
    out.cipherKeyCifrada = std::move(cipherKeyCifrada);
    out.authenticationKeyCifrada = std::move(authenticationKeyCifrada);
    out.dedicatedKeyCifrada = std::move(dedicatedKeyCifrada);
    co_return true;
}

boost::asio::awaitable<bool> DlmsProfileService::ResolveForDcu(
    const std::string& dcuSerial, DlmsClientProfile& out)
{
    int profileId = 0;
    std::string systemTitleCifrado;
    std::string cipherKeyCifrada;
    std::string authenticationKeyCifrada;
    std::string dedicatedKeyCifrada;
    try {
        auto conn = co_await m_provider();
        auto rs = co_await conn->execute_query(kResolveDcuQuery, { dcuSerial });
        if (rs.countRows() > 0) {
            DbRow row(rs, 0, kConnectionDataColumns);
            profileId = row.i("profile_id");
            systemTitleCifrado = row.s("system_title_cifrado");
            cipherKeyCifrada = row.s("cipher_key_cifrada");
            authenticationKeyCifrada = row.s("authentication_key_cifrada");
            dedicatedKeyCifrada = row.s("dedicated_key_cifrada");
        }
    }
    catch (const std::exception& e) {
        spdlog::error("ResolveForDcu({}) db error: {}", dcuSerial, e.what());
        co_return false;
    }

    if (profileId <= 0) {
        spdlog::error("DCU {} has no configured DLMS profile", dcuSerial);
        co_return false;
    }

    if (!GetByPk(profileId, out)) {
        spdlog::error("DCU {} resolved to profile id={} but it is not in cache",
            dcuSerial, profileId);
        co_return false;
    }

    out.systemTitleCifrado = std::move(systemTitleCifrado);
    out.cipherKeyCifrada = std::move(cipherKeyCifrada);
    out.authenticationKeyCifrada = std::move(authenticationKeyCifrada);
    out.dedicatedKeyCifrada = std::move(dedicatedKeyCifrada);
    co_return true;
}
