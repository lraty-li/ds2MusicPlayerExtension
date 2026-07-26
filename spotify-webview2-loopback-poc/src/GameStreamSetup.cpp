#include "PocApp.h"

void PocApp::HandleGameStreamEvent(GameStreamEvent event)
{
    if (event == GameStreamEvent::Pause)
    {
        AppendTelemetry("SESSION", L"game_stream_control=pause");
        ExecuteDiagnosticScript(
            L"window.__pocSpotifyControl && "
            L"window.__pocSpotifyControl('pause')");
    }
    else if (event == GameStreamEvent::Resume)
    {
        AppendTelemetry("SESSION", L"game_stream_control=resume");
        ExecuteDiagnosticScript(
            L"window.__pocSpotifyControl && "
            L"window.__pocSpotifyControl('resume')");
    }
    PostGameStreamState();
}

void PocApp::PostGameStreamState()
{
    PostJson(gameStreamClient_.MetricsJson());
}
