"""Exercise the production settings HTTP handlers with explicit admission doubles."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / '.pio' / 'settings-tests'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/web_http.cpp').read_text(encoding='utf-8')
start = source.index('esp_err_t settings_refusal(')
refusal = source[start:source.index('\n}\n', start) + 3]
start = source.index('esp_err_t get_settings(')
handlers = refusal + source[start:source.index('esp_err_t rejected(')]

stubs = r'''
#include "link_service.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
using esp_err_t = int;
constexpr size_t kSettingsCapacity = 1536;
struct httpd_req_t { std::string body_text; };
std::string status, response, header_value;
bool origin_ok = true, authorized = true, body_ok = true;
int sent = 0;
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *key, const char *) {
  header_value = key; return 0;
}
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *value) { status = value; return 0; }
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *) { return 0; }
esp_err_t httpd_resp_send(httpd_req_t *, const char *body, int) { response = body ? body : ""; ++sent; return 0; }
esp_err_t error(httpd_req_t *, const char *status_text, const char *body) {
  status = status_text; response = body ? body : ""; ++sent; return 0;
}
bool same_origin(httpd_req_t *) { return origin_ok; }
bool auth(httpd_req_t *) { return authorized; }
bool body(httpd_req_t *request, std::string &raw) {
  if (!body_ok) return false;
  raw = request->body_text;
  return true;
}
namespace link_service {
std::string settings_text = "{\"version\":1}";
bool settings_snapshot(char *out, size_t capacity) {
  if (settings_text.empty() || settings_text.size() + 1 > capacity) return false;
  std::memcpy(out, settings_text.c_str(), settings_text.size() + 1);
  return true;
}
link_operation::Kind last_kind = link_operation::Kind::None;
Transport last_transport = Transport::WiFi;
std::string last_id;
uint32_t last_revision = 0;
uint8_t last_profile = 255, last_mode = 255;
uint16_t last_seconds = 0, last_rate = 0;
unsigned test_requests = 0;
bool request_result = true, request_test_result = true, cancel_result = true;
link_operation::Reason forced_reason = link_operation::Reason::None;
bool request_operation(link_operation::Kind kind, Transport transport, const char *id, uint32_t revision,
                       link_operation::Reason &reason, uint8_t profile) {
  last_kind = kind; last_transport = transport; last_id = id ? id : ""; last_revision = revision;
  last_profile = profile;
  reason = forced_reason;
  return request_result;
}
bool request_test(Transport transport, const char *id, uint32_t revision, link_operation::Reason &reason,
                  uint8_t profile, uint16_t seconds, uint16_t rate, uint8_t mode) {
  ++test_requests;
  last_kind = link_operation::Kind::Test; last_transport = transport; last_id = id ? id : "";
  last_revision = revision; last_profile = profile; last_seconds = seconds; last_rate = rate; last_mode = mode;
  reason = forced_reason;
  return request_test_result;
}
bool cancel_operation(const char *id, link_operation::Reason &reason) {
  last_id = id ? id : ""; reason = forced_reason;
  return cancel_result;
}
}  // namespace link_service
'''

cases = r'''
int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string test = argv[1];
  const std::string id(32, 'a');
  httpd_req_t request;
  auto select_body = [&](const std::string &op, const std::string &transport, const std::string &extra) {
    return std::string("{\"id\":\"") + id + "\",\"revision\":3,\"op\":\"" + op + "\",\"transport\":\"" +
           transport + "\",\"confirm\":true" + extra + "}";
  };
  if (test == "select") {
    request.body_text = select_body("link.select", "sik", "");
    assert(post_settings(&request) == 0);
    assert(status == "202 Accepted" && response == "{\"state\":\"queued\"}");
    assert(link_service::last_kind == link_operation::Kind::Select);
    assert(link_service::last_transport == link_service::Transport::Radio);
    assert(link_service::last_id == id && link_service::last_revision == 3);
  } else if (test == "select_wifi") {
    request.body_text = select_body("link.select", "wifi", "");
    assert(post_settings(&request) == 0);
    assert(link_service::last_transport == link_service::Transport::WiFi);
  } else if (test == "test_profile_injected") {
    request.body_text = select_body("link.test", "sik", ",\"profile\":\"injected\"");
    assert(post_settings(&request) == 0);
    assert(status == "202 Accepted");
    assert(link_service::last_kind == link_operation::Kind::Test);
    assert(link_service::last_transport == link_service::Transport::Radio);
    assert(link_service::last_profile == 1);
    // An omitted shape keeps the canonical quick profile.
    assert(link_service::last_seconds == 30 && link_service::last_rate == 1000 && link_service::last_mode == 0);
    assert(link_service::test_requests == 1);
  } else if (test == "test_shape_passed") {
    request.body_text = select_body("link.test", "wifi", ",\"seconds\":120,\"rate\":200,\"mode\":2");
    assert(post_settings(&request) == 0);
    assert(status == "202 Accepted" && link_service::test_requests == 1);
    assert(link_service::last_seconds == 120 && link_service::last_rate == 200 && link_service::last_mode == 2);
    assert(link_service::last_profile == 0);
  } else if (test == "test_shape_canonical_default") {
    request.body_text = select_body("link.test", "wifi", "");
    assert(post_settings(&request) == 0);
    assert(status == "202 Accepted");
    assert(link_service::last_seconds == 30 && link_service::last_rate == 1000 && link_service::last_mode == 0);
  } else if (test == "test_shape_refused_combinations") {
    // A combination the tested medium does not offer is refused before it is
    // queued, whatever the other fields say.
    const char *refused[] = {
      ",\"profile\":\"injected\"",                    // Wi-Fi has no injected profile
      ",\"mode\":3",                                  // mode out of range
      ",\"seconds\":45",                              // no such duration
    };
    for (const char *extra : refused) {
      request.body_text = select_body("link.test", "wifi", extra);
      assert(post_settings(&request) == 0);
      assert(status == "400 Bad Request" && response == "{\"error\":\"request_refused\"}");
    }
    const char *radio_refused[] = {
      ",\"rate\":200",                                // the paired RTCM engine has no rate knob
      ",\"mode\":1",                                  // and no direction knob
    };
    for (const char *extra : radio_refused) {
      request.body_text = select_body("link.test", "sik", extra);
      assert(post_settings(&request) == 0);
      assert(status == "400 Bad Request" && response == "{\"error\":\"request_refused\"}");
    }
    assert(link_service::test_requests == 0);          // nothing was queued
    // The same values on the medium that does offer them are accepted.
    request.body_text = select_body("link.test", "sik", ",\"rate\":1000,\"mode\":0,\"profile\":\"injected\"");
    assert(post_settings(&request) == 0);
    assert(status == "202 Accepted" && link_service::test_requests == 1);
    assert(link_service::last_profile == 1 && link_service::last_rate == 1000 && link_service::last_mode == 0);
  } else if (test == "test_shape_type_rejected") {
    // A field of the wrong type is a malformed request, never a silent default.
    request.body_text = select_body("link.test", "wifi", ",\"seconds\":\"120\"");
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request" && response == "{\"error\":\"invalid_request\"}");
    request.body_text = select_body("link.test", "wifi", ",\"rate\":1000.5");
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request" && response == "{\"error\":\"invalid_request\"}");
    request.body_text = select_body("link.test", "wifi", ",\"mode\":-1");
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request" && response == "{\"error\":\"invalid_request\"}");
    assert(link_service::test_requests == 0);
  } else if (test == "test_profile_default") {
    request.body_text = select_body("link.test", "sik", "");
    assert(post_settings(&request) == 0);
    assert(status == "202 Accepted" && link_service::last_profile == 0);
  } else if (test == "test_profile_unsupported") {
    request.body_text = select_body("link.test", "sik", ",\"profile\":\"wild\"");
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request" && response == "{\"error\":\"profile_required\"}");
    assert(link_service::last_profile == 255);   // refused before any operation was queued
  } else if (test == "cancel") {
    request.body_text = "{\"id\":\"" + id + "\",\"op\":\"link.cancel\",\"confirm\":true}";
    assert(post_settings(&request) == 0);
    assert(status == "202 Accepted" && link_service::last_id == id);
  } else if (test == "test_op_unavailable") {
    request.body_text = select_body("link.test", "sik", "");
    link_service::request_test_result = false;
    link_service::forced_reason = link_operation::Reason::Unsupported;
    assert(post_settings(&request) == 0);
    assert(status == "503 Service Unavailable" && response == "{\"error\":\"test_operation_not_available\"}");
  } else if (test == "stale") {
    request.body_text = select_body("link.select", "sik", "");
    link_service::request_result = false;
    link_service::forced_reason = link_operation::Reason::StaleRevision;
    assert(post_settings(&request) == 0);
    assert(status == "409 Conflict" && response == "{\"error\":\"stale_revision\"}");
  } else if (test == "busy") {
    request.body_text = select_body("link.select", "sik", "");
    link_service::request_result = false;
    link_service::forced_reason = link_operation::Reason::Busy;
    assert(post_settings(&request) == 0);
    assert(status == "409 Conflict" && response == "{\"error\":\"operation_busy\"}");
  } else if (test == "conflict") {
    request.body_text = select_body("link.select", "sik", "");
    link_service::request_result = false;
    link_service::forced_reason = link_operation::Reason::ConflictingId;
    assert(post_settings(&request) == 0);
    assert(status == "409 Conflict" && response == "{\"error\":\"conflicting_id\"}");
  } else if (test == "cancelled") {
    request.body_text = select_body("link.select", "sik", "");
    link_service::request_result = false;
    link_service::forced_reason = link_operation::Reason::Cancelled;
    assert(post_settings(&request) == 0);
    assert(status == "409 Conflict" && response == "{\"error\":\"operation_cancelled\"}");
  } else if (test == "storage") {
    request.body_text = select_body("link.select", "sik", "");
    link_service::request_result = false;
    link_service::forced_reason = link_operation::Reason::StorageFailure;
    assert(post_settings(&request) == 0);
    assert(status == "503 Service Unavailable" && response == "{\"error\":\"link_storage_unavailable\"}");
  } else if (test == "origin") {
    origin_ok = false;
    request.body_text = select_body("link.select", "sik", "");
    assert(post_settings(&request) == 0);
    assert(status == "403 Forbidden" && response == "{\"error\":\"origin_rejected\"}");
  } else if (test == "unauthorized") {
    authorized = false;
    request.body_text = select_body("link.select", "sik", "");
    assert(post_settings(&request) == 0);
    assert(status == "401 Unauthorized" && response == "{\"error\":\"claim_control_first\"}");
  } else if (test == "malformed") {
    request.body_text = "{\"id\":\"x\"";
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request" && response == "{\"error\":\"invalid_request\"}");
  } else if (test == "oversize") {
    request.body_text = "{\"pad\":\"" + std::string(600, 'x') + "\"}";
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request" && response == "{\"error\":\"invalid_request\"}");
  } else if (test == "unreadable") {
    body_ok = false;
    request.body_text = select_body("link.select", "sik", "");
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request");
  } else if (test == "no_confirm") {
    request.body_text = "{\"id\":\"" + id + "\",\"revision\":3,\"op\":\"link.select\",\"transport\":\"sik\"}";
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request" && response == "{\"error\":\"confirm_required\"}");
  } else if (test == "no_revision") {
    request.body_text = "{\"id\":\"" + id + "\",\"op\":\"link.select\",\"transport\":\"sik\",\"confirm\":true}";
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request" && response == "{\"error\":\"revision_required\"}");
  } else if (test == "no_transport") {
    request.body_text = "{\"id\":\"" + id + "\",\"revision\":3,\"op\":\"link.select\",\"transport\":\"carrier\",\"confirm\":true}";
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request" && response == "{\"error\":\"transport_required\"}");
  } else if (test == "unknown_op") {
    request.body_text = select_body("link.reboot", "sik", "");
    assert(post_settings(&request) == 0);
    assert(status == "400 Bad Request" && response == "{\"error\":\"operation_not_available\"}");
  } else if (test == "get") {
    assert(get_settings(&request) == 0);
    assert(status == "200 OK" && response == "{\"version\":1}");
    assert(header_value == "X-Controller");
  } else if (test == "get_unavailable") {
    link_service::settings_text.clear();
    assert(get_settings(&request) == 0);
    assert(status == "503 Service Unavailable" && response == "{\"error\":\"settings_unavailable\"}");
  } else {
    return 2;
  }
  std::cout << "PASS: production settings HTTP " << test << "\n";
  return 0;
}
'''

generated = out / 'settings_http.cpp'
generated.write_text(stubs + handlers + cases, encoding='utf-8')
exe = out / 'settings_http.exe'
json_include = '-I' + str(root / '.pio/libdeps/unit_a/ArduinoJson/src')
subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'src'), json_include,
                str(generated), str(root / 'src/link_operation.cpp'), '-o', str(exe)], check=True)
for case in ['select', 'select_wifi', 'test_profile_injected', 'test_profile_default', 'test_profile_unsupported',
             'test_shape_passed', 'test_shape_canonical_default', 'test_shape_refused_combinations',
             'test_shape_type_rejected',
             'cancel', 'test_op_unavailable', 'stale', 'busy', 'conflict', 'cancelled',
             'storage', 'origin', 'unauthorized', 'malformed', 'oversize', 'unreadable', 'no_confirm',
             'no_revision', 'no_transport', 'unknown_op', 'get', 'get_unavailable']:
    subprocess.run([str(exe), case], check=True)
