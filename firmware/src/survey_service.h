#pragma once
#include "survey_engine.h"
#include "instrument_status.h"
void survey_begin(bool storage_ready);
void survey_update(const survey::Fix &fix);
// The bridge from the services to the engine (R10a slice 6), called by the
// loop: it applies one pending base request through the settings owner's gates
// (revision, role, idle profile, checked write and read-back, coordinate
// publish, profile re-apply, failure report) and then publishes the Fix the
// engine samples. `inputs` is the composition root's evaluation: its `now_ms` is
// the time the Fix and the derived link/correction facts are taken at.
void survey_publish(const instrument_status::Inputs &inputs);
bool survey_queue(const char *command);
bool survey_snapshot(char *output,size_t capacity);
bool survey_read(const char *query,char *output,size_t capacity);
bool survey_take_base(survey::BaseRequest &request);
bool survey_sd_lock();
void survey_sd_unlock();
// Latest accepted takeover controls either role. Bearers are not logged.
void survey_revoke_control();
int survey_claim(const char *client,char *token,size_t capacity);
bool survey_authorized(const char *token,bool renew=true);
void survey_release(const char *token);
bool survey_diagnostic_acquire();
void survey_diagnostic_release();
