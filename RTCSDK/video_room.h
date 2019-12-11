#pragma once

#include "plugin_client.h"
#include "Service/observable.h"
#include "i_video_room_listener.h"

namespace vi {
	class VideoRoom
		: public PluginClient
		, core::Observable
	{
	public:
		VideoRoom(std::shared_ptr<WebRTCServiceInterface> wrs);

		~VideoRoom();

		void addListener(std::shared_ptr<IVideoRoomListener> listener);

		void removeListener(std::shared_ptr<IVideoRoomListener> listener);

	protected:
		void onAttached(bool success) override;

		void onHangup() override;

		void onIceState(webrtc::PeerConnectionInterface::IceConnectionState iceState) override;

		void onMediaState(const std::string& media, bool on) override;

		void onWebrtcState(bool isActive, const std::string& reason) override;

		void onSlowLink(bool uplink, bool lost) override;

		void onMessage(const EventData& data, const Jsep& jsep) override;

		void onLocalStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;

		void onRemoteStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;

		void onData(const std::string& data, const std::string& label) override;

		void onDataOpen(const std::string& label) override;

		void onCleanup() override;

		void onDetached() override;

	protected:
		void publishOwnStream(bool audioOn);

		void unpublishOwnStream();

		void createParticipant(int64_t id, const std::string& displayName, const std::string& audio, const std::string& video);

	private:
		std::map<int64_t, std::shared_ptr<PluginClient>> _participantsMap;

		std::vector<std::weak_ptr<IVideoRoomListener>> _listeners;
	};
}
