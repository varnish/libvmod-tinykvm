extern "C" {
#include <adns/adns.h>
}
#include "adns.hpp"

namespace kvm {

union sys_adns_rules {
	struct {
		uint8_t ipv;
		uint8_t ttl;
		uint8_t port;
		uint8_t mode;
		uint8_t update;
		uint8_t nsswitch;
	} e;
	uint64_t reg;

	adns_rules to_rules() const {
		if (UNLIKELY(e.ipv >= ADNS_IPV__LAST))
			throw std::runtime_error("Invalid ADNS IPV value");
		if (UNLIKELY(e.ttl >= ADNS_TTL__LAST))
			throw std::runtime_error("Invalid ADNS TTL value");
		if (UNLIKELY(e.port >= ADNS_PORT__LAST))
			throw std::runtime_error("Invalid ADNS PORT value");
		if (UNLIKELY(e.mode >= ADNS_MODE__LAST))
			throw std::runtime_error("Invalid ADNS MODE value");
		if (UNLIKELY(e.update >= ADNS_UPDATE__LAST))
			throw std::runtime_error("Invalid ADNS UPDATE value");
		if (UNLIKELY(e.nsswitch >= ADNS_NSSWITCH__LAST))
			throw std::runtime_error("Invalid ADNS NSSWITCH value");
		return adns_rules {
			.magic = ADNS_RULES_MAGIC,
			.ipv = (adns_ipv_rule)e.ipv,
			.ttl = (adns_ttl_rule)e.ttl,
			.port = (adns_port_rule)e.port,
			.mode = (adns_mode_rule)e.mode,
			.update = (adns_update_rule)e.update,
			.nsswitch = (adns_nsswitch_rule)e.nsswitch
		};
	}
};

void libadns_untag(const std::string& tag, struct vcl* vcl)
{
	ADNS_untag(tag.c_str(), vcl);
}
static AsyncDNSEntry from_list_entry(const struct adns_info *info)
{
	AsyncDNSEntry entry;
	if (info->addr_a) {
		entry.name = std::string(info->addr_a);
		entry.ipv  = 4u;
	} else {
		entry.name = std::string(info->addr_aaaa);
		entry.ipv  = 6u;
	}
	entry.priority = info->priority;
	entry.touched  = info->state == ADNS_INFO_UPDATED;
	entry.weight   = info->weight;
	return entry;
}
void libadns_update(struct adns_info_list *info_list,
    struct adns_hints *hints, void *priv)
{
	(void) hints;
	assert(priv != nullptr && info_list != nullptr);
	auto* adns = (kvm::AsyncDNS *)priv;
	std::unique_lock lck(adns->mtx);
	//printf("Update called\n");

	adns->entries.clear();
	struct adns_info *info = nullptr;
	VTAILQ_FOREACH(info, &info_list->head, list) {
		adns->entries.push_back(from_list_entry(info));
	}
}

static void syscall_adns_new(vCPU& cpu, MachineInstance& inst)
{
	const int idx = inst.program().free_adns_tag();
	auto& entry = inst.program().m_adns_tags.at(idx);

	auto& regs = cpu.registers();
	if (!inst.is_storage() || idx < 0) {
		regs.rax = -1;
		cpu.set_registers(regs);
		return;
	}
	/**
	 * RDI: tag key, added to the program instance address to build program-unique tag
	 * XXX: Tag is not fully unique due to program address not being random enough.
	 **/
	std::string tag = std::to_string((uint32_t)regs.rdi) + std::to_string((uintptr_t)&inst.program());

	// XXX: ***Enforce that we own the tag here***

	auto ret = ADNS_tag(tag.c_str(), inst.program().get_adns_key());
	if (ret < 0) {
		inst.logf("libadns: Tag %s failed (%s)",
			tag.c_str(), ADNS_err(ret));
		regs.rax = -1;
	} else {
		entry.tag = std::move(tag);
		// int ADNS_subscribe(const char *tag, struct vcl *vcl,
		//		adns_update_cb_f *update_cb, void *priv);
		ADNS_subscribe(entry.tag.c_str(), inst.program().get_adns_key(), libadns_update, &entry);
		regs.rax = idx;
	}
	cpu.set_registers(regs);
}
static void syscall_adns_free(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	if (!inst.is_storage()) {
		regs.rax = -1;
		cpu.set_registers(regs);
		return;
	}
	/**
	 * RDI: tag index
	 **/
	const uint32_t idx = regs.rdi;
	auto& tag = inst.program().m_adns_tags.at(idx).tag;

	if (!tag.empty()) {
		ADNS_untag(tag.c_str(), inst.program().get_adns_key());
		tag = "";
		regs.rax = 0;
	} else {
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}

static void syscall_adns_config(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	if (!inst.is_storage()) {
		regs.rax = -1;
		cpu.set_registers(regs);
		return;
	}
	/**
	 * RDI: tag index
	 * RSI: host
	 * RDX: service
	 * RCX: TTL
	 * R8: rules
	 * R9: hints
	 **/
	const uint32_t idx = regs.rdi;
	auto& entry = inst.program().m_adns_tags.at(idx);

	const auto host = cpu.machine().copy_from_cstring(regs.rsi);
	const auto srv = cpu.machine().copy_from_cstring(regs.rdx);
	const float ttl = regs.rcx / 1024.0;
	auto rules = sys_adns_rules{ .reg = regs.r8 }.to_rules();
	adns_hints* hints = nullptr;

	auto ret = ADNS_config(entry.tag.c_str(), inst.program().get_adns_key(),
		host.c_str(), srv.c_str(), ttl, &rules, hints);
	if (ret != 0) {
		inst.logf("libadns: Configuration on %s failed (%s)",
			entry.tag.c_str(), ADNS_err(ret));
		regs.rax = -1;
	} else {
		regs.rax = 0;
	}
	cpu.set_registers(regs);
}

struct g_adns_entry {
	uint8_t  ipv;
	uint8_t  addrlen;
	std::array<uint8_t, AsyncDNSEntry::ADDR_LEN> addr;
	int8_t   touched;
	uint16_t priority;
	uint16_t weight;
	std::array<uint8_t, AsyncDNSEntry::HASH_LEN> hash;
};

static void syscall_adns_get(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	/**
	 * RDI: tag index
	 * RSI: entry index
	 * RDX: adns buffer
	 * RCX: adns maxlen
	 * R8:  host buffer
	 * R9:  host maxlen
	 **/
	const uint32_t tag = regs.rdi;
	const uint32_t idx = regs.rsi;
	const uint64_t vbuffer = regs.rdx;
	const size_t   vbuflen = regs.rcx;
	auto& adns = inst.program().m_adns_tags.at(tag);

	std::unique_lock lck(adns.mtx);
	if (UNLIKELY(adns.tag.empty())) {
		throw std::runtime_error("ADNS tag not initialized");
	}
	if (vbuffer == 0x0) {
		regs.rax = adns.entries.size();
	}
	else if (idx < adns.entries.size() && vbuflen >= sizeof(g_adns_entry)) {
		const auto& entry = adns.entries.at(idx);
		g_adns_entry gentry {
			.ipv     = entry.ipv,
			.addrlen = (uint8_t)entry.name.size(),
			.touched = entry.touched,
			.priority = entry.priority,
			.weight  = entry.weight,
			.hash    = entry.hash,
		};
		const uint8_t addrlen = uint8_t(entry.name.size() + 1);
		if (addrlen <= AsyncDNSEntry::ADDR_LEN) {
			std::memcpy(gentry.addr.data(), entry.name.c_str(), addrlen);
			cpu.machine().copy_to_guest(vbuffer, &gentry, sizeof(gentry));
			regs.rax = 0;
		} else {
			regs.rax = -1;
		}
	} else {
		if (adns.entries.size() == 0) {
			printf("Warning: ADNS tag had no entries\n");
		}
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}

struct BlockingADNSData
{
	AsyncDNSEntry entry;
};

static void blocking_adns_callback(struct adns_info_list *info_list,
    struct adns_hints *hints, void *priv)
{
	(void) hints;
	assert(priv != nullptr && info_list != nullptr);
	auto* adns = (BlockingADNSData *)priv;

	AsyncDNSEntry entry;
	struct adns_info *info = nullptr;
	VTAILQ_FOREACH(info, &info_list->head, list) {
		adns->entry = from_list_entry(info);
		return;
	}
	// Not found
}

std::string adns_interp(struct vcl *vcl, std::string url)
{
	auto off = url.find("://${");
	if (off != std::string::npos) {
		auto offe = url.find("}", off);
		if (offe != std::string::npos) {
			auto name_off = off + 5;
			auto dns_tag = url.substr(name_off, offe - name_off);
			//printf("DNS tag: %s\n", dns_tag.c_str());
			const char *ctag = dns_tag.c_str();

			// Refcount tag keeping it alive.
			if (ADNS_tag(ctag, vcl) == 0)
			{
				BlockingADNSData data;
				// Receive DNS updates on tag in callback.
				ADNS_subscribe(ctag, vcl, blocking_adns_callback, &data);
				// Blocking wait for refresh in order to get address in callback.
				ADNS_refresh(ctag, vcl, ADNS_REFRESH_INFO, 1);
				// Unsubscribe as we do not have a way to re-use the subscription afterwards
				ADNS_unsubscribe(ctag, vcl, blocking_adns_callback, &data);
				// Release refcount on tag.
				ADNS_untag(ctag, vcl);

				if (UNLIKELY(data.entry.name.empty())) {
					// The ADNS resolution didn't happen, reject request.
					throw std::runtime_error("ADNS tag not resolved: " + dns_tag);
				}

				// Re-construct URI with new address, adding ://.
				return url.substr(0, off+3) + data.entry.name + url.substr(offe+1);
			}
		}
	}
	return url;
}

} // kvm
