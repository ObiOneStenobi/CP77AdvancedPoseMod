#pragma once

namespace APM::ScriptApi
{
// Registers the APM_* global native functions. Call from the RTTI post-register callback.
void Register();
} // namespace APM::ScriptApi
