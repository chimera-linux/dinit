#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <set>

#include <dinit.h>
#include <service.h>
#include <baseproc-sys.h>
#include <proc-service.h>
#include <control.h>
#include "control-datatypes.h"

#include "../test_service.h"
#include "../test_procservice.h"

// Control protocol tests.
#ifdef NDEBUG
#error "This file must be built with assertions ENABLED!"
#endif

// common communication datatypes
using namespace dinit_cptypes;

class control_conn_t_test
{
    public:
    static service_record * service_from_handle(control_conn_t *cc, handle_t handle)
    {
        return cc->find_service_for_key(handle);
    }
};

// Size of status buffer, as returned in several packet types
constexpr static int STATUS_BUFFER_SIZE = 6 + ((sizeof(pid_t) > sizeof(int)) ? sizeof(pid_t) : sizeof(int));
constexpr static int STATUS_BUFFER5_SIZE = 6 + 2 * sizeof(int);

void cptest_queryver()
{
    service_set sset;
    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    bp_sys::supply_read_data(fd, { (char)cp_cmd::QUERYVERSION });

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    // Write will process immediately, so there's no need for this:
    //event_loop.regd_bidi_watchers[fd]->write_ready(event_loop, fd);

    // We expect a version number back:
    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 5);
    assert(wdata[0] == (char)cp_rply::CPVERSION);

    delete cc;
}

void cptest_listservices()
{
    service_set sset;

    service_record *s1 = new service_record(&sset, "test-service-1", service_type_t::INTERNAL, {});
    sset.add_service(s1);
    service_record *s2 = new service_record(&sset, "test-service-2", service_type_t::INTERNAL, {});
    sset.add_service(s2);
    service_record *s3 = new service_record(&sset, "test-service-3", service_type_t::INTERNAL, {});
    sset.add_service(s3);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    bp_sys::supply_read_data(fd, { (char)cp_cmd::LISTSERVICES });

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    // Write will process immediately, so there's no need for this:
    //event_loop.regd_bidi_watchers[fd]->write_ready(event_loop, fd);

    // We expect, for each service:
    // (1 byte)   cp_rply::SVCINFO
    // (1 byte)   service name length
    // (1 byte)   state
    // (1 byte)   target state
    // (1 byte)   flags: has console, waiting for console, start skipped
    // (1 byte)   stop reason
    // (2 bytes)  reserved
    // (? bytes)  exit status (int) / process id (pid_t)
    // (N bytes)  service name

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    std::set<std::string> names = {"test-service-1", "test-service-2", "test-service-3"};

    int pos = 0;
    for (int i = 0; i < 3; i++) {
        assert(wdata[pos++] == (char)cp_rply::SVCINFO);
        unsigned char name_len_c = wdata[pos++];
        pos += 6;

        pos += std::max(sizeof(int), sizeof(pid_t));

        std::string name;
        for (int j = 0; j < (int)name_len_c; j++) {
            name += wdata[pos++];
        }

        // Check the service name matches one from the set, and remove it:
        auto fn = names.find(name);
        assert (fn != names.end());
        names.erase(fn);
    }

    delete cc;
}

static handle_t  find_service(int fd, const char *service_name,
        service_state_t expected_state, service_state_t expected_target_state)
{
    std::vector<char> cmd = { (char)cp_cmd::FINDSERVICE };
    uint16_t name_len = strlen(service_name);
    char *name_len_cptr = reinterpret_cast<char *>(&name_len);
    cmd.insert(cmd.end(), name_len_cptr, name_len_cptr + sizeof(name_len));
    cmd.insert(cmd.end(), service_name, service_name + name_len);

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    // We expect:
    // (1 byte)   cp_rply::SERVICERECORD
    // (1 byte)   state
    // (handle_t) handle
    // (1 byte)   target state

    assert(wdata.size() == 3 + sizeof(handle_t));
    assert(wdata[0] == (char)cp_rply::SERVICERECORD);
    service_state_t s = static_cast<service_state_t>(wdata[1]);
    assert(s == expected_state);
    service_state_t ts = static_cast<service_state_t>(wdata[6]);
    assert(ts == expected_target_state);

    handle_t h1;
    std::copy(wdata.data() + 2, wdata.data() + 2 + sizeof(h1), reinterpret_cast<char *>(&h1));

    return h1;
}

void cptest_findservice1()
{
    service_set sset;

    const char * const service_name_2 = "test-service-2";

    service_record *s1 = new service_record(&sset, "test-service-1", service_type_t::INTERNAL, {});
    sset.add_service(s1);
    service_record *s2 = new service_record(&sset, "test-service-2", service_type_t::INTERNAL, {});
    sset.add_service(s2);
    service_record *s3 = new service_record(&sset, "test-service-3", service_type_t::INTERNAL, {});
    sset.add_service(s3);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    find_service(fd, service_name_2, service_state_t::STOPPED, service_state_t::STOPPED);

    delete cc;
}

void cptest_findservice2()
{
    service_set sset;

    const char * const service_name_2 = "test-service-2";

    service_record *s1 = new service_record(&sset, "test-service-1", service_type_t::INTERNAL, {});
    sset.add_service(s1);
    service_record *s2 = new service_record(&sset, "test-service-2", service_type_t::INTERNAL, {});
    sset.add_service(s2);
    service_record *s3 = new service_record(&sset, "test-service-3", service_type_t::INTERNAL, {});
    sset.add_service(s3);

    sset.start_service(s2);
    sset.process_queues();

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    find_service(fd, service_name_2, service_state_t::STARTED, service_state_t::STARTED);

    delete cc;
}

// test finding non-existing service
void cptest_findservice3()
{
    service_set sset;

    const char * const service_name_2 = "test-service-n";

    service_record *s1 = new service_record(&sset, "test-service-1", service_type_t::INTERNAL, {});
    sset.add_service(s1);
    service_record *s2 = new service_record(&sset, "test-service-2", service_type_t::INTERNAL, {});
    sset.add_service(s2);
    service_record *s3 = new service_record(&sset, "test-service-3", service_type_t::INTERNAL, {});
    sset.add_service(s3);

    sset.start_service(s2);
    sset.process_queues();

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    std::vector<char> cmd = { (char)cp_cmd::FINDSERVICE };
    uint16_t name_len = strlen(service_name_2);
    char *name_len_cptr = reinterpret_cast<char *>(&name_len);
    cmd.insert(cmd.end(), name_len_cptr, name_len_cptr + sizeof(name_len));
    cmd.insert(cmd.end(), service_name_2, service_name_2 + name_len);

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    // We expect:
    // (1 byte)   cp_rply::NOSERVICE

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::NOSERVICE);

    delete cc;
}

class test_service_set : public service_set
{
    public:
    service_record * service1 = nullptr;
    service_record * service2 = nullptr;

    virtual service_record *load_service(const char *name) override
    {
        auto r = find_service(name);
        if (r == nullptr) {
            if (strcmp(name, "test-service-1") == 0) {
                service1 = new service_record(this, "test-service-1");
                add_service(service1);
                return service1;
            }
            else if (strcmp(name, "test-service-2") == 0) {
                service2 = new service_record(this, "test-service-2");
                add_service(service2);
                return service2;
            }
            throw service_not_found(name);
        }
        return r;
    }
};

void cptest_loadservice()
{
    test_service_set sset;

    const char * const service_name_1 = "test-service-1";
    const char * const service_name_2 = "test-service-2";

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    std::vector<char> cmd = { (char)cp_cmd::LOADSERVICE };
    uint16_t name_len = strlen(service_name_1);
    char *name_len_cptr = reinterpret_cast<char *>(&name_len);
    cmd.insert(cmd.end(), name_len_cptr, name_len_cptr + sizeof(name_len));
    cmd.insert(cmd.end(), service_name_1, service_name_1 + name_len);

    bp_sys::supply_read_data(fd, std::move(cmd));
    bp_sys::set_blocking(fd);

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    // We expect:
    // (1 byte)   cp_rply::SERVICERECORD
    // (1 byte)   state
    // (handle_t) handle
    // (1 byte)   target state

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 3 + sizeof(handle_t));
    assert(wdata[0] == (char)cp_rply::SERVICERECORD);
    service_state_t s = static_cast<service_state_t>(wdata[1]);
    assert(s == service_state_t::STOPPED);
    service_state_t ts = static_cast<service_state_t>(wdata[6]);
    assert(ts == service_state_t::STOPPED);

    assert(sset.service1 != nullptr);
    assert(sset.service2 == nullptr);

    cmd = { (char)cp_cmd::LOADSERVICE };
    cmd.insert(cmd.end(), name_len_cptr, name_len_cptr + sizeof(name_len));
    cmd.insert(cmd.end(), service_name_2, service_name_2 + name_len);

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    // We expect:
    // (1 byte)   cp_rply::SERVICERECORD
    // (1 byte)   state
    // (handle_t) handle
    // (1 byte)   target state

    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 3 + sizeof(handle_t));
    assert(wdata[0] == (char)cp_rply::SERVICERECORD);
    s = static_cast<service_state_t>(wdata[1]);
    assert(s == service_state_t::STOPPED);
    ts = static_cast<service_state_t>(wdata[6]);
    assert(ts == service_state_t::STOPPED);

    assert(sset.service1 != nullptr);
    assert(sset.service2 != nullptr);

    delete cc;
}

void cptest_startstop()
{
    service_set sset;

    const char * const service_name = "test-service-1";

    service_record *s1 = new service_record(&sset, "test-service-1", service_type_t::INTERNAL, {});
    sset.add_service(s1);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    // Get a service handle:
    handle_t h = find_service(fd, service_name, service_state_t::STOPPED,
            service_state_t::STOPPED);

    // Issue start:
    std::vector<char> cmd = { (char)cp_cmd::STARTSERVICE, 0 /* don't pin */ };
    char * h_cp = reinterpret_cast<char *>(&h);
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h));

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 1 + 7 + STATUS_BUFFER_SIZE /* ACK reply + info packet */
            + 7 + STATUS_BUFFER5_SIZE /* + v5 info packet */);

    // First info packet (v5 protocol):
    assert(wdata[0] == (char)cp_info::SERVICEEVENT5);
    handle_t ip_h;
    std::copy(wdata.data() + 2, wdata.data() + 2 + sizeof(ip_h),
            reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h);
    assert(wdata[6] == static_cast<int>(service_event_t::STARTED));

    // 2nd info packet (original protocol):
    unsigned idx = 7 + STATUS_BUFFER5_SIZE;
    assert(wdata[idx] == (char)cp_info::SERVICEEVENT);
    // packetsize, key (handle), event
    assert(wdata[idx + 1] == 7 + STATUS_BUFFER_SIZE);
    std::copy(wdata.data() + idx + 2, wdata.data() + idx + 2 + sizeof(ip_h), reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h);
    assert(wdata[idx + 6] == static_cast<int>(service_event_t::STARTED));

    // Reply packet:
    constexpr unsigned reply_start = 7 + STATUS_BUFFER_SIZE + 7 + STATUS_BUFFER5_SIZE;
    // we get ALREADYSS since it started immediately:
    assert(wdata[reply_start] == (char)cp_rply::ALREADYSS);
    assert(s1->get_state() == service_state_t::STARTED);

    // Issue stop:
    cmd = { (char)cp_cmd::STOPSERVICE, 0 /* don't pin */ };
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h));

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 1 + 7 + STATUS_BUFFER_SIZE + 7 + STATUS_BUFFER5_SIZE);

    // v5 status packet:
    assert(wdata[0] == (char)cp_info::SERVICEEVENT5);
    // packet size, handle, event
    assert(wdata[1] == 7 + STATUS_BUFFER5_SIZE);
    std::copy(wdata.data() + 2, wdata.data() + 2 + sizeof(ip_h), reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h);
    assert(wdata[idx + 6] == static_cast<int>(service_event_t::STOPPED));

    // Original status packet:
    idx = 7 + STATUS_BUFFER5_SIZE;
    assert(wdata[idx] == (char)cp_info::SERVICEEVENT);
    assert(wdata[idx + 1] == 7 + STATUS_BUFFER_SIZE);
    std::copy(wdata.data() + 2, wdata.data() + 2 + sizeof(ip_h), reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h);
    assert(wdata[6] == static_cast<int>(service_event_t::STOPPED));

    // we get ALREADYSS since it stopped immediately:
    assert(wdata[reply_start] == (char)cp_rply::ALREADYSS);
    assert(s1->get_state() == service_state_t::STOPPED);

    delete cc;
}

void cptest_start_pinned()
{
    service_set sset;

    const char * const service_name = "test-service-1";

    service_record *s1 = new service_record(&sset, "test-service-1", service_type_t::INTERNAL, {});
    sset.add_service(s1);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    s1->pin_stop();

    // Get a service handle:
    handle_t h = find_service(fd, service_name, service_state_t::STOPPED,
            service_state_t::STOPPED);

    // Issue start:
    std::vector<char> cmd = { (char)cp_cmd::STARTSERVICE, 0 /* don't pin */ };
    char * h_cp = reinterpret_cast<char *>(&h);
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h));

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 1 /* cp_rply::PINNEDSTOPPED */);
    assert(wdata[0] == (char)cp_rply::PINNEDSTOPPED);

    delete cc;
}

void cptest_gentlestop()
{
    service_set sset;

    const char * const test1_name = "test-service-1";

    service_record *s1 = new service_record(&sset, test1_name, service_type_t::INTERNAL, {});
    sset.add_service(s1);

    service_record *s2 = new service_record(&sset, "test-service-2", service_type_t::INTERNAL,
            {{s1, dependency_type::REGULAR}});
    sset.add_service(s2);

    // Start the services:
    sset.start_service(s2);
    assert(s1->get_state() == service_state_t::STARTED);
    assert(s2->get_state() == service_state_t::STARTED);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    handle_t h = find_service(fd, test1_name, service_state_t::STARTED,
            service_state_t::STARTED);

    char * h_cp = reinterpret_cast<char *>(&h);

    // Issue stop:
    std::vector<char> cmd = { (char)cp_cmd::STOPSERVICE, 2 /* don't pin, gentle */ };
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h));

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    // We expect:
    // 1 byte: cp_rply::DEPENDENTS
    // size_t: number of handles (N)
    // N * handle_t: handles for dependents that would be stopped

    assert(wdata.size() == (1 + sizeof(size_t) + sizeof(handle_t)));
    assert(wdata[0] == (char)cp_rply::DEPENDENTS);

    size_t nhandles;
    memcpy(&nhandles, wdata.data() + 1, sizeof(nhandles));
    assert(nhandles == 1);

    handle_t rhandle;
    memcpy(&rhandle, wdata.data() + 1 + sizeof(size_t), sizeof(rhandle));

    service_record * rservice = control_conn_t_test::service_from_handle(cc, rhandle);
    assert(rservice == s2);

    delete cc;
}

void cptest_queryname()
{
    service_set sset;

    const char * const test1_name = "test-service-1";

    service_record *s1 = new service_record(&sset, test1_name, service_type_t::INTERNAL, {});
    sset.add_service(s1);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    // Get a service handle:
    handle_t h = find_service(fd, test1_name, service_state_t::STOPPED,
            service_state_t::STOPPED);

    char * h_cp = reinterpret_cast<char *>(&h);

    // Issue name query:
    std::vector<char> cmd = { (char)cp_cmd::QUERYSERVICENAME, 0 /* reserved */ };
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h));

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    // We expect:
    // 1 byte packet type = cp_rply::SERVICENAME
    // 1 byte reserved
    // uint16_t length
    // N bytes name

    assert(wdata.size() == (2 + sizeof(uint16_t) + strlen(test1_name)));
    assert(wdata[0] == (char)cp_rply::SERVICENAME);
    assert(wdata[1] == 0);
    uint16_t len;
    memcpy(&len, wdata.data() + 2, sizeof(uint16_t));
    assert(len == strlen(test1_name));

    assert(strncmp(wdata.data() + 2 + sizeof(uint16_t), test1_name, strlen(test1_name)) == 0);

    delete cc;
}

void cptest_unload()
{
    service_set sset;

    const char * const service_name1 = "test-service-1";
    const char * const service_name2 = "test-service-2";

    service_record *s1 = new service_record(&sset, service_name1, service_type_t::INTERNAL, {});
    sset.add_service(s1);
    service_record *s2 = new service_record(&sset, service_name2, service_type_t::INTERNAL,
            {{ s1, dependency_type::WAITS_FOR }});
    sset.add_service(s2);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    handle_t h1 = find_service(fd, service_name1, service_state_t::STOPPED,
            service_state_t::STOPPED);

    // Issue unload:
    std::vector<char> cmd = { (char)cp_cmd::UNLOADSERVICE };
    char * h_cp = reinterpret_cast<char *>(&h1);
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h1));

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    // We should receive NAK, as the service has a dependency:
    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::NAK);


    handle_t h2 = find_service(fd, service_name2, service_state_t::STOPPED,
            service_state_t::STOPPED);

    // Issue unload for s2:

    cmd = { (char)cp_cmd::UNLOADSERVICE };
    h_cp = reinterpret_cast<char *>(&h2);
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h2));

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    // We should receive ACK:
    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::ACK);

    // Now try to unload s1 again:

    cmd = { (char)cp_cmd::UNLOADSERVICE };
    h_cp = reinterpret_cast<char *>(&h1);
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h1));

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    // We should receive ACK:
    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::ACK);

    // If we try to FIND service 1 now, it should not be there:
    cmd = { (char)cp_cmd::FINDSERVICE };
    uint16_t name_len = strlen(service_name1);
    char *name_len_cptr = reinterpret_cast<char *>(&name_len);
    cmd.insert(cmd.end(), name_len_cptr, name_len_cptr + sizeof(name_len));
    cmd.insert(cmd.end(), service_name1, service_name1 + name_len);

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::NOSERVICE);

    delete cc;
}

void cptest_addrmdeps()
{
    service_set sset;

    const char * const service_name1 = "test-service-1";
    const char * const service_name2 = "test-service-2";

    service_record *s1 = new service_record(&sset, service_name1, service_type_t::INTERNAL, {});
    sset.add_service(s1);
    service_record *s2 = new service_record(&sset, service_name2, service_type_t::INTERNAL, {});
    sset.add_service(s2);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    handle_t h1 = find_service(fd, service_name1, service_state_t::STOPPED,
            service_state_t::STOPPED);
    handle_t h2 = find_service(fd, service_name2, service_state_t::STOPPED,
            service_state_t::STOPPED);

    // Add dep from s1 -> s2:
    std::vector<char> cmd = { (char)cp_cmd::ADD_DEP, static_cast<char>(dependency_type::REGULAR) };
    char * h1cp = reinterpret_cast<char *>(&h1);
    char * h2cp = reinterpret_cast<char *>(&h2);
    cmd.insert(cmd.end(), h1cp, h1cp + sizeof(h1));
    cmd.insert(cmd.end(), h2cp, h2cp + sizeof(h2));

    bp_sys::supply_read_data(fd, std::move(cmd));
    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);
    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::ACK);

    // Issue start for S1. S2 should also start:
    cmd = { (char)cp_cmd::STARTSERVICE, 0 /* don't pin */ };
    cmd.insert(cmd.end(), h1cp, h1cp + sizeof(h1));

    bp_sys::supply_read_data(fd, std::move(cmd));
    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);
    bp_sys::extract_written_data(fd, wdata);

    // ACK + 2x2 info packets
    assert(wdata.size() == 1 + (7 + STATUS_BUFFER_SIZE) * 2 + (7 + STATUS_BUFFER5_SIZE) * 2);
    assert(s1->get_state() == service_state_t::STARTED);
    assert(s2->get_state() == service_state_t::STARTED);

    // Remove dependency from S1 -> S2:
    cmd = { (char)cp_cmd::REM_DEP, static_cast<char>(dependency_type::REGULAR) };
    cmd.insert(cmd.end(), h1cp, h1cp + sizeof(h1));
    cmd.insert(cmd.end(), h2cp, h2cp + sizeof(h2));

    bp_sys::supply_read_data(fd, std::move(cmd));
    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);
    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 1 + 7 + STATUS_BUFFER_SIZE + 7 + STATUS_BUFFER5_SIZE); // ACK + info packet
    assert(s2->get_state() == service_state_t::STOPPED);

    delete cc;
}

void cptest_enableservice()
{
    service_set sset;

    const char * const service_name1 = "test-service-1";
    const char * const service_name2 = "test-service-2";

    service_record *s1 = new service_record(&sset, service_name1, service_type_t::INTERNAL, {});
    sset.add_service(s1);
    service_record *s2 = new service_record(&sset, service_name2, service_type_t::INTERNAL, {});
    sset.add_service(s2);

    s1->start();
    sset.process_queues();

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    handle_t h1 = find_service(fd, service_name1, service_state_t::STARTED, service_state_t::STARTED);
    handle_t h2 = find_service(fd, service_name2, service_state_t::STOPPED, service_state_t::STOPPED);

    // Enable from s1 -> s2:
    std::vector<char> cmd = { (char)cp_cmd::ENABLESERVICE, static_cast<char>(dependency_type::WAITS_FOR) };
    char * h1cp = reinterpret_cast<char *>(&h1);
    char * h2cp = reinterpret_cast<char *>(&h2);
    cmd.insert(cmd.end(), h1cp, h1cp + sizeof(h1));
    cmd.insert(cmd.end(), h2cp, h2cp + sizeof(h2));

    bp_sys::supply_read_data(fd, std::move(cmd));
    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 1 + 7 + STATUS_BUFFER_SIZE + 7 + STATUS_BUFFER5_SIZE /* ACK reply + 2x info packet */);

    // v5 service event:
    assert(wdata[0] == (char)cp_info::SERVICEEVENT5);
    // size, handle, event
    assert(wdata[1] == 7 + STATUS_BUFFER5_SIZE);
    handle_t ip_h;
    std::copy(wdata.data() + 2, wdata.data() + 2 + sizeof(ip_h), reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h2);
    assert(wdata[6] == static_cast<int>(service_event_t::STARTED));

    // Original service event:
    unsigned idx = 7 + STATUS_BUFFER5_SIZE;
    assert(wdata[idx] == (char)cp_info::SERVICEEVENT);
    // packetsize, key (handle), event
    assert(wdata[idx + 1] == 7 + STATUS_BUFFER_SIZE);
    std::copy(wdata.data() + idx + 2, wdata.data() + idx + 2 + sizeof(ip_h),
            reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h2);
    assert(wdata[idx + 6] == static_cast<int>(service_event_t::STARTED));

    // and then the ack:
    assert(wdata[7 + STATUS_BUFFER_SIZE + 7 + STATUS_BUFFER5_SIZE] == (char)cp_rply::ACK);

    sset.process_queues();

    // We expect that s2 is now started:
    assert(s2->get_state() == service_state_t::STARTED);

    s1->stop();
    sset.process_queues();

    assert(s2->get_state() == service_state_t::STOPPED);

    bp_sys::extract_written_data(fd, wdata);

    delete cc;
}

void cptest_restart()
{
    service_set sset;

    const char * const service_name = "test-service-1";

    test_service *s1 = new test_service(&sset, "test-service-1", service_type_t::INTERNAL, {});
    sset.add_service(s1);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    // Get a service handle:
    handle_t h = find_service(fd, service_name, service_state_t::STOPPED,
            service_state_t::STOPPED);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 0);

    // Issue restart:
    std::vector<char> cmd = { (char)cp_cmd::STOPSERVICE, 4 /* restart */ };
    char * h_cp = reinterpret_cast<char *>(&h);
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h));

    bp_sys::supply_read_data(fd, cmd);

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 1 /* NAK reply, wrong state */);
    assert(wdata[0] == (char)cp_rply::NAK);

    // Start the service now:
    s1->start();
    sset.process_queues();
    s1->started();
    sset.process_queues();

    bp_sys::extract_written_data(fd, wdata);

    // Issue restart (again):
    bp_sys::supply_read_data(fd, std::move(cmd));
    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);
    bp_sys::extract_written_data(fd, wdata);

    // info packet (service stopped) x 2 + ACK:
    assert(wdata.size() == 7 + STATUS_BUFFER_SIZE + 7 + STATUS_BUFFER5_SIZE + 1);

    // v5 info packet:
    assert(wdata[0] == (char)cp_info::SERVICEEVENT5);
    assert(wdata[1] == 7 + STATUS_BUFFER5_SIZE);
    handle_t ip_h;
    std::copy(wdata.data() + 2, wdata.data() + 2 + sizeof(ip_h), reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h);
    assert(wdata[6] == static_cast<int>(service_event_t::STOPPED));

    // Original info packet:
    unsigned idx = 7 + STATUS_BUFFER5_SIZE;
    assert(wdata[idx] == (char)cp_info::SERVICEEVENT);
    assert(wdata[idx + 1] == 7 + STATUS_BUFFER_SIZE);
    std::copy(wdata.data() + idx + 2, wdata.data() + idx + 2 + sizeof(ip_h),
            reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h);
    assert(wdata[idx + 6] == static_cast<int>(service_event_t::STOPPED));

    // ACK:
    idx += 7 + STATUS_BUFFER_SIZE;
    assert(wdata[idx] == (char)cp_rply::ACK);

    sset.process_queues();
    assert(s1->get_state() == service_state_t::STARTING);

    s1->started();
    sset.process_queues();
    assert(s1->get_state() == service_state_t::STARTED);

    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 7 + STATUS_BUFFER_SIZE + 7 + STATUS_BUFFER5_SIZE);  /* info packets */

    assert(wdata[0] == (char)cp_info::SERVICEEVENT5);
    assert(wdata[1] == 7 + STATUS_BUFFER5_SIZE);
    std::copy(wdata.data() + 2, wdata.data() + 2 + sizeof(ip_h), reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h);
    assert(wdata[6] == static_cast<int>(service_event_t::STARTED));

    idx = 7 + STATUS_BUFFER5_SIZE;
    assert(wdata[idx] == (char)cp_info::SERVICEEVENT);
    assert(wdata[idx + 1] == 7 + STATUS_BUFFER_SIZE);
    std::copy(wdata.data() + idx + 2, wdata.data() + idx + 2 + sizeof(ip_h),
            reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h);
    assert(wdata[idx + 6] == static_cast<int>(service_event_t::STARTED));

    delete cc;
}

void cptest_wake()
{
    service_set sset;

    const char * const service_name1 = "test-service-1";
    const char * const service_name2 = "test-service-2";

    service_record *s1 = new service_record(&sset, service_name1, service_type_t::INTERNAL, {});
    sset.add_service(s1);
    service_record *s2 = new service_record(&sset, service_name2, service_type_t::INTERNAL,
            {{ s1, dependency_type::WAITS_FOR }});
    sset.add_service(s2);

    s2->start();
    sset.process_queues();

    s1->stop(true);
    sset.process_queues();

    assert(s1->get_state() == service_state_t::STOPPED);
    assert(s2->get_state() == service_state_t::STARTED);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    handle_t h1 = find_service(fd, service_name1, service_state_t::STOPPED,
            service_state_t::STOPPED);

    // Wake s1:
    std::vector<char> cmd = { (char)cp_cmd::WAKESERVICE, 0 /* don't pin */ };
    char * h_cp = reinterpret_cast<char *>(&h1);
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h1));
    bp_sys::supply_read_data(fd, std::move(cmd));

    std::vector<char> wdata;
    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);
    bp_sys::extract_written_data(fd, wdata);

    // ACK + 2 x info packet
    assert(wdata.size() == 1 + 7 + STATUS_BUFFER_SIZE + 7 + STATUS_BUFFER5_SIZE);

    // v5 info packet:
    assert(wdata[0] == (char)cp_info::SERVICEEVENT5);
    assert(wdata[1] == 7 + STATUS_BUFFER5_SIZE);
    handle_t ip_h;
    std::copy(wdata.data() + 2, wdata.data() + 2 + sizeof(ip_h), reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h1);
    assert(wdata[6] == static_cast<int>(service_event_t::STARTED));

    // Original info packet:
    unsigned idx = 7 + STATUS_BUFFER5_SIZE;
    assert(wdata[idx] == (char)cp_info::SERVICEEVENT);
    assert(wdata[idx + 1] == 7 + STATUS_BUFFER_SIZE);
    std::copy(wdata.data() + idx + 2, wdata.data() + idx + 2 + sizeof(ip_h),
            reinterpret_cast<char *>(&ip_h));
    assert(ip_h == h1);
    assert(wdata[idx + 6] == static_cast<int>(service_event_t::STARTED));

    // and then the ack (already started):
    idx += 7 + STATUS_BUFFER_SIZE;
    assert(wdata[idx] == (char)cp_rply::ALREADYSS);

    // now stop s2 (and therefore s1):
    s2->stop(true);
    sset.process_queues();
    assert(s1->get_state() == service_state_t::STOPPED);
    assert(s2->get_state() == service_state_t::STOPPED);

    // Clear any info packets:
    bp_sys::extract_written_data(fd, wdata);

    // Trying to wake s1 should now fail:
    cmd = { (char)cp_cmd::WAKESERVICE, 0 /* don't pin */ };
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h1));
    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);
    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::NAK);

    delete cc;
}

void cptest_servicestatus()
{
    service_set sset;

    service_record *s1 = new service_record(&sset, "test-service-1", service_type_t::INTERNAL, {});
    sset.add_service(s1);
    service_record *s2 = new service_record(&sset, "test-service-2", service_type_t::INTERNAL, {});
    sset.add_service(s2);
    service_record *s3 = new service_record(&sset, "test-service-3", service_type_t::INTERNAL, {});
    sset.add_service(s3);

    s2->start();
    sset.process_queues();

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    auto STOPPED = service_state_t::STOPPED;
    auto STARTED = service_state_t::STARTED;
    handle_t h1 = find_service(fd, "test-service-1", STOPPED, STOPPED);
    handle_t h2 = find_service(fd, "test-service-2", STARTED, STARTED);
    handle_t h3 = find_service(fd, "test-service-3", STOPPED, STOPPED);

    std::vector<char> cmd = { (char)cp_cmd::SERVICESTATUS };
    char * h_cp = reinterpret_cast<char *>(&h1);
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h1));
    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    constexpr static int STATUS_BUFFER_SIZE = 6 + ((sizeof(pid_t) > sizeof(int)) ? sizeof(pid_t) : sizeof(int));

    // 1 byte: cp_rply::SERVICESTATUS
    // 1 byte: reserved
    // STATUS_BUFFER_SIZE bytes: status
    assert(wdata.size() == (2 + STATUS_BUFFER_SIZE));
    assert(wdata[0] == (char)cp_rply::SERVICESTATUS);
    assert(wdata[2] == (int)service_state_t::STOPPED); // state
    assert(wdata[3] == (int)service_state_t::STOPPED); // target state
    assert(wdata[4] == 0); // various flags

    cmd.clear();
    cmd.push_back((char)cp_cmd::SERVICESTATUS);
    h_cp = reinterpret_cast<char *>(&h2);
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h1));
    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == (2 + STATUS_BUFFER_SIZE));
    assert(wdata[0] == (char)cp_rply::SERVICESTATUS);
    assert(wdata[2] == (int)service_state_t::STARTED); // state
    assert(wdata[3] == (int)service_state_t::STARTED); // target state
    assert(wdata[4] == 8); // various flags; 8 = marked active

    (void)h3; // silence warning

    delete cc;
}

void cptest_sendsignal()
{
    using namespace std;

    service_set sset;
    ha_string command = "test-command";
    list<pair<unsigned,unsigned>> command_offsets;
    command_offsets.emplace_back(0, command.length());
    std::list<prelim_dep> depends;

    process_service p {&sset, "test-service", std::move(command), command_offsets, depends};
    init_service_defaults(p);
    sset.add_service(&p);

    p.start();
    sset.process_queues();
    base_process_service_test::exec_succeeded(&p);
    sset.process_queues();

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    // Get a service handle:
    handle_t h = find_service(fd, "test-service", service_state_t::STARTED,
            service_state_t::STARTED);

    // Prepare a signal: (SIGHUP for example)
    sig_num_t sig = SIGHUP;

    // Issue a signal:
    std::vector<char> cmd = { (char)cp_cmd::SIGNAL };
    char * sig_cp = reinterpret_cast<char *>(&sig);
    char * h_cp = reinterpret_cast<char *>(&h);
    cmd.insert(cmd.end(), sig_cp, sig_cp + sizeof(sig));
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h));

    bp_sys::supply_read_data(fd, std::move(cmd));
    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::ACK);

    assert(bp_sys::last_sig_sent == SIGHUP);

    // Prepare an another signal: (for sure)
    sig = SIGILL;

    // Issue a signal:
    cmd = { (char)cp_cmd::SIGNAL };
    sig_cp = reinterpret_cast<char *>(&sig);
    cmd.insert(cmd.end(), sig_cp, sig_cp + sizeof(sig));
    cmd.insert(cmd.end(), h_cp, h_cp + sizeof(h));

    bp_sys::supply_read_data(fd, std::move(cmd));
    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    wdata.clear();
    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::ACK);

    assert(bp_sys::last_sig_sent == SIGILL);

    sset.remove_service(&p);

    delete cc;
}

// Two commands in one packet
void cptest_two_commands()
{
    service_set sset;

    const char * const service_name_1 = "test-service-1";
    const char * const service_name_2 = "test-service-2";
    const char * const service_name_3 = "test-service-3";

    service_record *s1 = new service_record(&sset, service_name_1, service_type_t::INTERNAL, {});
    sset.add_service(s1);
    service_record *s2 = new service_record(&sset, service_name_2, service_type_t::INTERNAL, {});
    sset.add_service(s2);
    service_record *s3 = new service_record(&sset, service_name_3, service_type_t::INTERNAL, {});
    sset.add_service(s3);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    std::vector<char> cmd = { (char)cp_cmd::FINDSERVICE };
    uint16_t name_len = strlen(service_name_1);
    char *name_len_cptr = reinterpret_cast<char *>(&name_len);
    cmd.insert(cmd.end(), name_len_cptr, name_len_cptr + sizeof(name_len));
    cmd.insert(cmd.end(), service_name_1, service_name_1 + name_len);

    // Insert a 2nd FINDSERVICE command into the same vector:
    cmd.push_back((char)cp_cmd::FINDSERVICE);
    name_len = strlen(service_name_2);
    cmd.insert(cmd.end(), name_len_cptr, name_len_cptr + sizeof(name_len));
    cmd.insert(cmd.end(), service_name_2, service_name_2 + name_len);

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    // We expect 2x:
    // (1 byte)   cp_rply::SERVICERECORD
    // (1 byte)   state
    // (handle_t) handle
    // (1 byte)   target state

    assert(wdata.size() == 2 * (3 + sizeof(handle_t)));
    assert(wdata[0] == (char)cp_rply::SERVICERECORD);
    assert(wdata[3 + sizeof(handle_t)] == (char)cp_rply::SERVICERECORD);

    delete cc;
}

void cptest_closehandle()
{
    service_set sset;

    const char * const service_name_1 = "test-service-1";

    service_record *s1 = new service_record(&sset, service_name_1, service_type_t::INTERNAL, {});
    sset.add_service(s1);

    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    handle_t hndl = find_service(fd, service_name_1, service_state_t::STOPPED, service_state_t::STOPPED);

    std::vector<char> cmd = { (char)cp_cmd::CLOSEHANDLE };
    cmd.insert(cmd.end(), (char *)&hndl, (char *)&hndl + sizeof(hndl));
    bp_sys::supply_read_data(fd, std::move(cmd));
    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::ACK);

    delete cc;
}

void cptest_invalid()
{
    service_set sset;
    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    // (char)-1 is here because it will not be a valid packet type
    std::vector<char> cmd = { (char)-1 };
    bp_sys::supply_read_data(fd, cmd);

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);

    assert(wdata.size() == 1);
    assert(wdata[0] == (char)cp_rply::BADREQ);

    // Make sure dinit will not read further commands
    int current_watch = event_loop.regd_bidi_watchers[fd]->get_watches(event_loop);
    assert(current_watch == dasynq::OUT_EVENTS);

    delete cc;
}

void cptest_envevent()
{
    service_set sset;
    int fd = bp_sys::allocfd();
    auto *cc = new control_conn_t(event_loop, &sset, fd);

    // Listen on environment:
    std::vector<char> cmd = { (char)cp_cmd::LISTENENV };

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    std::vector<char> wdata;
    bp_sys::extract_written_data(fd, wdata);
    assert(wdata.size() == 1 /* ACK reply */);

    // Issue a setenv:
    cmd = { (char)cp_cmd::SETENV };

    const char *envn = "FOO=bar";
    envvar_len_t envl = strlen(envn);
    cmd.insert(cmd.end(), (char *)&envl, ((char *)&envl) + sizeof(envl));
    cmd.insert(cmd.end(), envn, envn + envl);

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    bp_sys::extract_written_data(fd, wdata);

    // Environment event
    // packet type (1), packet length (1), flags (1), data (envl + 1)
    assert(wdata[0] == (char)cp_info::ENVEVENT);
    assert(wdata[1] == 3 + sizeof(envl));
    assert(wdata[2] == 0);
    envl += 1; // null terminator
    assert(memcmp(&envl, &wdata[3], sizeof(envl)) == 0);
    assert(strcmp(&wdata[3 + sizeof(envl)], envn) == 0);

    // Override setenv
    cmd = { (char)cp_cmd::SETENV };

    envn = "FOO=baz";
    envl = strlen(envn);
    cmd.insert(cmd.end(), (char *)&envl, ((char *)&envl) + sizeof(envl));
    cmd.insert(cmd.end(), envn, envn + envl);

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    bp_sys::extract_written_data(fd, wdata);

    // Environment event
    assert(wdata[0] == (char)cp_info::ENVEVENT);
    assert(wdata[1] == 3 + sizeof(envl));
    assert(wdata[2] != 0);
    envl += 1; // null terminator
    assert(memcmp(&envl, &wdata[3], sizeof(envl)) == 0);
    assert(strcmp(&wdata[3 + sizeof(envl)], envn) == 0);

    // Unset setenv
    cmd = { (char)cp_cmd::SETENV };

    envn = "FOO";
    envl = strlen(envn);
    cmd.insert(cmd.end(), (char *)&envl, ((char *)&envl) + sizeof(envl));
    cmd.insert(cmd.end(), envn, envn + envl);

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    bp_sys::extract_written_data(fd, wdata);

    assert(wdata[0] == (char)cp_info::ENVEVENT);
    assert(wdata[1] == 3 + sizeof(envl));
    assert(wdata[2] != 0);
    envl += 1; // null terminator
    assert(memcmp(&envl, &wdata[3], sizeof(envl)) == 0);
    assert(strcmp(&wdata[3 + sizeof(envl)], envn) == 0);

    // Unset setenv again to check override flag
    cmd = { (char)cp_cmd::SETENV };

    envn = "FOO";
    envl = strlen(envn);
    cmd.insert(cmd.end(), (char *)&envl, ((char *)&envl) + sizeof(envl));
    cmd.insert(cmd.end(), envn, envn + envl);

    bp_sys::supply_read_data(fd, std::move(cmd));

    event_loop.regd_bidi_watchers[fd]->read_ready(event_loop, fd);

    bp_sys::extract_written_data(fd, wdata);

    assert(wdata[0] == (char)cp_info::ENVEVENT);
    assert(wdata[1] == 3 + sizeof(envl));
    assert(wdata[2] == 0);
    envl += 1; // null terminator
    assert(memcmp(&envl, &wdata[3], sizeof(envl)) == 0);
    assert(strcmp(&wdata[3 + sizeof(envl)], envn) == 0);

    delete cc;
}



#define RUN_TEST(name, spacing) \
    std::cout << #name "..." spacing << std::flush; \
    name(); \
    std::cout << "PASSED" << std::endl;

int main(int argc, char **argv)
{
    RUN_TEST(cptest_queryver, "           ");
    RUN_TEST(cptest_listservices, "       ");
    RUN_TEST(cptest_findservice1, "       ");
    RUN_TEST(cptest_findservice2, "       ");
    RUN_TEST(cptest_findservice3, "       ");
    RUN_TEST(cptest_loadservice, "        ");
    RUN_TEST(cptest_startstop, "          ");
    RUN_TEST(cptest_start_pinned, "       ");
    RUN_TEST(cptest_gentlestop, "         ");
    RUN_TEST(cptest_queryname, "          ");
    RUN_TEST(cptest_unload, "             ");
    RUN_TEST(cptest_addrmdeps, "          ");
    RUN_TEST(cptest_enableservice, "      ");
    RUN_TEST(cptest_restart, "            ");
    RUN_TEST(cptest_wake, "               ");
    RUN_TEST(cptest_servicestatus, "      ");
    RUN_TEST(cptest_sendsignal, "         ");
    RUN_TEST(cptest_two_commands, "       ");
    RUN_TEST(cptest_closehandle, "        ");
    RUN_TEST(cptest_invalid, "            ");
    RUN_TEST(cptest_envevent, "           ");
    return 0;
}
