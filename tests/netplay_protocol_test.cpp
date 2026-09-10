#include "Common/SFMLHelper.h"
#include "Core/Boot/Boot.h"
#include "Core/HW/WiimoteEmu/DesiredWiimoteState.h"
#include "Core/IOS/FS/FileSystem.h"
#include "Core/NetPlay/NetPlayClient.h"
#include "Core/NetPlay/NetPlayServer.h"
#include "UICommon/UICommon.h"
#include "moderngekko/cpu_state.h"
#include "moderngekko/runtime.hpp"
#include "netplay_compatibility.hpp"
#include "core/native_state_layout.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {
int DispatchA(CPUState *, std::uint32_t) { return 0; }
int DispatchB(CPUState *, std::uint32_t) { return 0; }

constexpr ModernGekkoRange module_ranges[] = {
    {0x80003100u, 0x80003120u},
};
constexpr std::uint64_t first_hashes[] = {0x123456789abcdef0u};
constexpr std::uint64_t second_hashes[] = {0x123456789abcdef0u};
constexpr std::uint64_t changed_hashes[] = {0x123456789abcdef1u};
const ModernGekkoNativeMetadata native_metadata = {
    "llvm", "arm64-apple-ios17.0", "LLVM 20.1.8", "test-codegen",
    "test-build", dolnative_state_layout_hash()};

const ModernGekkoModuleDesc first_descriptor = {
    MODERNGEKKO_MODULE_ABI_VERSION,
    MODERNGEKKO_CPU_ABI_VERSION,
    sizeof(CPUState),
    "TEST01",
    0x80003100u,
    DispatchA,
    nullptr,
    module_ranges,
    1,
    nullptr,
    0,
    module_ranges,
    1,
    first_hashes,
    nullptr,
    0,
    &native_metadata,
    nullptr,
};

const ModernGekkoModuleDesc second_descriptor = {
    MODERNGEKKO_MODULE_ABI_VERSION,
    MODERNGEKKO_CPU_ABI_VERSION,
    sizeof(CPUState),
    "TEST01",
    0x80003100u,
    DispatchB,
    nullptr,
    module_ranges,
    1,
    nullptr,
    0,
    module_ranges,
    1,
    second_hashes,
    nullptr,
    0,
    &native_metadata,
    nullptr,
};

const ModernGekkoModuleDesc changed_descriptor = {
    MODERNGEKKO_MODULE_ABI_VERSION,
    MODERNGEKKO_CPU_ABI_VERSION,
    sizeof(CPUState),
    "TEST01",
    0x80003100u,
    DispatchB,
    nullptr,
    module_ranges,
    1,
    nullptr,
    0,
    module_ranges,
    1,
    changed_hashes,
    nullptr,
    0,
    &native_metadata,
    nullptr,
};
} // namespace

class TestUI final : public NetPlay::NetPlayUI {
public:
  void BootGame(const std::string &,
                std::unique_ptr<BootSessionData>) override {}
  void StopGame() override {}
  bool IsHosting() const override { return false; }
  void Update() override {}
  void AppendChat(const std::string &) override {}
  void OnMsgChangeGame(const NetPlay::SyncIdentifier &,
                       const std::string &) override {}
  void OnMsgChangeGBARom(int, const NetPlay::GBAConfig &) override {}
  void OnMsgStartGame() override {}
  void OnMsgStopGame() override {}
  void OnMsgPowerButton() override {}
  void OnPlayerConnect(const std::string &) override {}
  void OnPlayerDisconnect(const std::string &) override {}
  void OnPadBufferChanged(u32 value) override { buffer = value; }
  void OnHostInputAuthorityChanged(bool) override {}
  void OnDesync(u32, const std::string &) override {}
  void OnConnectionLost() override {}
  void OnConnectionError(const std::string &message) override {
    error = message;
  }
  void OnTraversalError(Common::TraversalClient::FailureReason) override {}
  void OnTraversalStateChanged(Common::TraversalClient::State) override {}
  void OnGameStartAborted() override {}
  void OnGolferChanged(bool, const std::string &) override {}
  void OnTtlDetermined(u8) override {}
  bool IsRecording() override { return false; }
  std::shared_ptr<const UICommon::GameFile>
  FindGameFile(const NetPlay::SyncIdentifier &,
               NetPlay::SyncIdentifierComparison *found) override {
    if (found)
      *found = NetPlay::SyncIdentifierComparison::DifferentGame;
    return {};
  }
  std::string FindGBARomPath(const std::array<u8, 20> &, std::string_view,
                             int) override {
    return {};
  }
  void ShowGameDigestDialog(const std::string &) override {}
  void SetGameDigestProgress(int, int) override {}
  void SetGameDigestResult(int, const std::string &) override {}
  void AbortGameDigest() override {}
  void OnIndexAdded(bool, std::string) override {}
  void OnIndexRefreshFailed(std::string) override {}
  void ShowChunkedProgressDialog(const std::string &, u64,
                                 std::span<const int>) override {}
  void HideChunkedProgressDialog() override {}
  void SetChunkedProgress(int, u64) override {}
  void SetHostWiiSyncData(std::vector<u64>, std::string) override {}

  std::string error;
  std::atomic<u32> buffer{0};
};

bool WaitFor(const auto &condition) {
  for (int i = 0; i < 100; ++i) {
    if (condition())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

int main() {
  moderngekko::GameMetadata metadata;
  metadata.disc_id = "TEST01";
  metadata.dol_sha256 = "dol";
  moderngekko::RuntimeConfig first_config;
  first_config.module =
      moderngekko::ModuleSource::AttachedDescriptor(&first_descriptor);
  moderngekko::RuntimeConfig second_config;
  second_config.module =
      moderngekko::ModuleSource::AttachedDescriptor(&second_descriptor);
  moderngekko::RuntimeConfig changed_config;
  changed_config.module =
      moderngekko::ModuleSource::AttachedDescriptor(&changed_descriptor);
  const std::string first_fingerprint =
      moderngekko::frontend::CompatibilityFingerprint(first_config, metadata);
  if (first_fingerprint !=
      moderngekko::frontend::CompatibilityFingerprint(second_config, metadata))
    return 11;
  if (first_fingerprint ==
      moderngekko::frontend::CompatibilityFingerprint(changed_config, metadata))
    return 12;

  const auto directory =
      std::filesystem::temp_directory_path() / "moderngekko-netplay-test";
  std::filesystem::remove_all(directory);
  const auto mods_directory = directory / "Mods";
  std::filesystem::create_directories(mods_directory);
  std::filesystem::copy_file(
      MODERNGEKKO_NETPLAY_TEST_MOD_PATH,
      mods_directory / std::filesystem::path(MODERNGEKKO_NETPLAY_TEST_MOD_PATH).filename());
  moderngekko::RuntimeConfig modded_config = first_config;
  modded_config.mod_directories.push_back(mods_directory);
  const std::string modded_fingerprint =
      moderngekko::frontend::CompatibilityFingerprint(modded_config, metadata);
  if (modded_fingerprint == first_fingerprint ||
      modded_fingerprint !=
          moderngekko::frontend::CompatibilityFingerprint(modded_config, metadata))
    return 13;
  UICommon::SetUserDirectory(directory.string());
  UICommon::Init();

  TestUI host_ui;
  TestUI first_ui;
  TestUI second_ui;
  TestUI third_ui;
  TestUI invalid_ui;
  auto invalid = std::make_unique<NetPlay::NetPlayClient>(
      "invalid host", 2626, &invalid_ui, "Invalid",
      NetPlay::NetTraversalConfig{}, 1);
  if (invalid->IsConnected() || invalid_ui.error.empty())
    return 10;
  invalid.reset();
  NetPlay::SetCompatibilityFingerprint("matching-build");
  auto server = std::make_unique<NetPlay::NetPlayServer>(
      0, false, &host_ui, NetPlay::NetTraversalConfig{});
  if (!server->is_connected)
    return 1;
  std::jthread lobby_observer([&](std::stop_token stop) {
    while (!stop.stop_requested()) {
      static_cast<void>(server->CanStart());
      std::this_thread::yield();
    }
  });
  auto first = std::make_unique<NetPlay::NetPlayClient>(
      "127.0.0.1", server->GetPort(), &first_ui, "First",
      NetPlay::NetTraversalConfig{}, 3);
  if (!first->IsConnected() ||
      !WaitFor([&] { return first->GetAssignedControllerCount() == 3; }))
    return 2;
  auto second = std::make_unique<NetPlay::NetPlayClient>(
      "127.0.0.1", server->GetPort(), &second_ui, "Second",
      NetPlay::NetTraversalConfig{}, 2);
  if (!second->IsConnected() ||
      !WaitFor([&] { return second->GetAssignedControllerCount() == 1; }))
    return 3;
  if (!WaitFor([&] { return first->GetPlayersSnapshot().size() == 2; }))
    return 4;

  first->SetLocalControllerCount(1);
  if (!WaitFor([&] { return first->GetAssignedControllerCount() == 1; }))
    return 14;
  auto third = std::make_unique<NetPlay::NetPlayClient>(
      "127.0.0.1", server->GetPort(), &third_ui, "Third",
      NetPlay::NetTraversalConfig{}, 1);
  if (!third->IsConnected() ||
      !WaitFor([&] { return third->GetAssignedControllerCount() == 1; }) ||
      !WaitFor([&] { return first->GetPlayersSnapshot().size() == 3; }))
    return 15;

  server->AdjustPadBufferSize(2);
  server->SetAdaptiveBuffer(true);
  if (!WaitFor([&] {
        return first_ui.buffer == 2 && second_ui.buffer == 2 &&
               third_ui.buffer == 2;
      }))
    return 18;
  sf::Packet buffer_request;
  buffer_request << NetPlay::MessageID::PadBufferRequest
                 << static_cast<u32>(12);
  first->SendAsync(std::move(buffer_request));
  if (!WaitFor([&] {
        return first_ui.buffer == 4 && second_ui.buffer == 4 &&
               third_ui.buffer == 4;
      }))
    return 17;

  sf::Packet input;
  input << NetPlay::MessageID::WiimoteData << static_cast<NetPlay::PadIndex>(0)
        << static_cast<u8>(3);
  const std::array<u8, 3> sent_state = {0x12, 0x34, 0x56};
  input.append(sent_state.data(), sent_state.size());
  first->SendAsync(std::move(input), NetPlay::INPUT_CHANNEL);
  WiimoteEmu::SerializedWiimoteState received_state{};
  NetPlay::NetPlayClient::WiimoteDataBatchEntry entry = {0, &received_state};
  if (!WaitFor([&] {
        return second->WiimoteUpdate(std::span(&entry, 1)) &&
               received_state.length == sent_state.size() &&
               std::ranges::equal(sent_state,
                                  std::span(received_state.data.data(),
                                            received_state.length));
      }))
    return 13;
  received_state = {};
  if (!WaitFor([&] {
        return third->WiimoteUpdate(std::span(&entry, 1)) &&
               received_state.length == sent_state.size() &&
               std::ranges::equal(sent_state,
                                  std::span(received_state.data.data(),
                                            received_state.length));
      }))
    return 16;

  third.reset();
  second.reset();
  first.reset();
  lobby_observer.request_stop();
  lobby_observer.join();
  server.reset();

  NetPlay::SetCompatibilityFingerprint("host-build");
  server = std::make_unique<NetPlay::NetPlayServer>(
      0, false, &host_ui, NetPlay::NetTraversalConfig{});
  if (!server->is_connected)
    return 5;
  NetPlay::SetCompatibilityFingerprint("guest-build");
  auto mismatch = std::make_unique<NetPlay::NetPlayClient>(
      "127.0.0.1", server->GetPort(), &second_ui, "Mismatch",
      NetPlay::NetTraversalConfig{}, 1);
  if (mismatch->IsConnected() || second_ui.error.empty() ||
      mismatch->GetConnectionError() !=
          NetPlay::ConnectionError::CompatibilityMismatch)
    return 6;

  mismatch.reset();
  server.reset();

  // Nearby iPhone sessions assign GameCube ports, independent of the desktop
  // Wii lobby. Exercise four phones, ready gating, routing, capacity and reuse.
  NetPlay::SetCompatibilityFingerprint("gamecube-build");
  server = std::make_unique<NetPlay::NetPlayServer>(
      0, false, &host_ui, NetPlay::NetTraversalConfig{},
      NetPlay::ControllerMode::GameCube, true);
  if (!server->is_connected) return 30;
  std::array<TestUI, 4> gc_ui;
  std::array<std::unique_ptr<NetPlay::NetPlayClient>, 4> gc;
  for (std::size_t i = 0; i < gc.size(); ++i) {
    gc[i] = std::make_unique<NetPlay::NetPlayClient>(
        "127.0.0.1", server->GetPort(), &gc_ui[i], "Phone " + std::to_string(i + 1),
        NetPlay::NetTraversalConfig{}, 1, NetPlay::ControllerMode::GameCube);
    if (!gc[i]->IsConnected()) return 31;
  }
  if (!WaitFor([&] {
        const auto mapping = gc[0]->GetPadMappingSnapshot();
        return mapping == NetPlay::PadMappingArray{1, 2, 3, 4} &&
               gc[3]->GetPadMappingSnapshot() == mapping &&
               gc[0]->GetWiimoteMappingSnapshot() == NetPlay::PadMappingArray{};
      })) return 32;

  // Low ping must not prevent a GameCube room from retaining enough input
  // headroom for repeated stalls. All four clients receive the same target.
  server->AdjustPadBufferSize(2);
  server->SetAdaptiveBuffer(true);
  const auto all_buffers = [&](u32 size) {
    return std::ranges::all_of(gc_ui, [size](const TestUI& ui) {
      return ui.buffer == size;
    });
  };
  if (!WaitFor([&] { return all_buffers(6); })) return 44;
  sf::Packet gc_buffer_request;
  gc_buffer_request << NetPlay::MessageID::PadBufferRequest << u32{10};
  gc[0]->SendAsync(std::move(gc_buffer_request));
  if (!WaitFor([&] { return all_buffers(10); })) return 45;
  // The old policy discarded a stall boost after four seconds.
  std::this_thread::sleep_for(std::chrono::milliseconds(4500));
  if (!all_buffers(10)) return 46;
  sf::Packet excessive_buffer_request;
  excessive_buffer_request << NetPlay::MessageID::PadBufferRequest << u32{20};
  gc[3]->SendAsync(std::move(excessive_buffer_request));
  if (!WaitFor([&] { return all_buffers(12); })) return 47;

  if (server->CanStart()) return 33;
  // A client cannot mark itself ready while game compatibility is unknown.
  gc[0]->SetReady(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  if (server->CanStart()) return 34;
  for (auto& phone : gc) {
    sf::Packet status;
    status << NetPlay::MessageID::GameStatus << NetPlay::SyncIdentifierComparison::SameGame;
    phone->SendAsync(std::move(status));
    phone->SetReady(true);
  }
  if (!WaitFor([&] { return server->CanStart(); })) return 35;
  gc[2]->SetReady(false);
  if (!WaitFor([&] { return !server->CanStart(); })) return 36;

  TestUI full_ui;
  auto full = std::make_unique<NetPlay::NetPlayClient>(
      "127.0.0.1", server->GetPort(), &full_ui, "Fifth phone", NetPlay::NetTraversalConfig{},
      1, NetPlay::ControllerMode::GameCube);
  if (full->IsConnected() || full->GetConnectionError() != NetPlay::ConnectionError::RoomFull)
    return 37;
  full.reset();
  TestUI wrong_mode_ui;
  auto wrong_mode = std::make_unique<NetPlay::NetPlayClient>(
      "127.0.0.1", server->GetPort(), &wrong_mode_ui, "Wii", NetPlay::NetTraversalConfig{});
  if (wrong_mode->IsConnected() ||
      wrong_mode->GetConnectionError() != NetPlay::ConnectionError::CompatibilityMismatch)
    return 38;
  wrong_mode.reset();

  // Send a complete GameCube input from player 1; the other phones must see
  // identical buttons, axes and triggers at the same emulated controller port.
  sf::Packet pad;
  pad << NetPlay::MessageID::PadData << static_cast<NetPlay::PadIndex>(0)
      << static_cast<u16>(0x0100) << static_cast<u8>(255) << static_cast<u8>(0)
      << static_cast<u8>(180) << static_cast<u8>(90) << static_cast<u8>(128)
      << static_cast<u8>(129) << static_cast<u8>(80) << static_cast<u8>(20) << true;
  gc[0]->SendAsync(std::move(pad), NetPlay::INPUT_CHANNEL);
  for (int i = 1; i < 4; ++i) {
    GCPadStatus received{};
    if (!WaitFor([&] { return gc[i]->GetNetPads(0, false, &received); }) ||
        received.button != 0x0100 || received.analogA != 255 || received.stickX != 180 ||
        received.stickY != 90 || received.substickY != 129 || received.triggerLeft != 80 ||
        received.triggerRight != 20 || !received.isConnected) return 39;
  }
  gc[2].reset();
  if (!WaitFor([&] { return gc[0]->GetPadMappingSnapshot()[2] == 0; })) return 40;
  gc[2] = std::make_unique<NetPlay::NetPlayClient>(
      "127.0.0.1", server->GetPort(), &gc_ui[2], "Replacement phone", NetPlay::NetTraversalConfig{},
      1, NetPlay::ControllerMode::GameCube);
  if (!gc[2]->IsConnected() || !WaitFor([&] {
        return gc[0]->GetPadMappingSnapshot() == NetPlay::PadMappingArray{1, 2, 3, 4};
      })) return 41;
  if (server->CanStart()) return 42; // A replacement must explicitly ready up.
  // A phone cannot submit another player's inputs.
  sf::Packet forged;
  forged << NetPlay::MessageID::PadData << static_cast<NetPlay::PadIndex>(0);
  gc[2]->SendAsync(std::move(forged), NetPlay::INPUT_CHANNEL);
  if (!WaitFor([&] { return gc[0]->GetPadMappingSnapshot()[2] == 0; })) return 43;
  for (auto& phone : gc) phone.reset();
  server.reset();
  UICommon::Shutdown();
  std::filesystem::remove_all(directory);
  return 0;
}
