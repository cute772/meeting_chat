#include "video_room.h"
#include "string_utils.h"
#include <QDebug>
#include "participant.h"
#include "x2struct.hpp"
#include "thread_manager.h"
#include "Service/app_instance.h"

namespace vi {
	VideoRoom::VideoRoom(std::shared_ptr<WebRTCServiceInterface> wrs)
		: PluginClient(wrs)
	{
		_pluginContext->plugin = "janus.plugin.videoroom";
		_pluginContext->opaqueId = "videoroom-" + StringUtils::randomString(12);
	}

	VideoRoom::~VideoRoom()
	{
		if (_pluginContext->webrtcContext->pc) {
			_pluginContext->webrtcContext->pc->Close();
		}
	}

	void VideoRoom::init()
	{
		// All callbacks are in main thread
		rtc::Thread* mainThread = rtcApp->getThreadManager()->getMainThread();
		auto listener = std::make_shared<VideoRoomListener>();
		_listenerProxy = VideoRoomListenerProxy::Create(mainThread, listener);
	}

	void VideoRoom::addListener(std::shared_ptr<IVideoRoomListener> listener)
	{
		_listenerProxy->attach(listener);
	}

	void VideoRoom::removeListener(std::shared_ptr<IVideoRoomListener> listener)
	{
		_listenerProxy->detach(listener);
	}

	std::shared_ptr<PluginClient> VideoRoom::getParticipant(int64_t pid)
	{
		if (pid == this->_id) {
			return shared_from_this();
		}
		else {
			return _participantsMap.find(pid) == _participantsMap.end() ? nullptr : _participantsMap[pid];
		}
	}

	void VideoRoom::onAttached(bool success)
	{
		if (success) {
			qDebug() << "Plugin attached! (" << _pluginContext->plugin.c_str() << ", id=" << _id << ")";
			qDebug() << "  -- This is a publisher/manager";
		}
		else {
			qDebug() << "  -- Error attaching plugin...";
		}
	}

	void VideoRoom::onHangup() {}

	void VideoRoom::onIceState(webrtc::PeerConnectionInterface::IceConnectionState iceState) {}

	void VideoRoom::onMediaState(const std::string& media, bool on) 
	{
		qDebug() << "Janus " << (on ? "started" : "stopped") << " receiving our " << media.c_str();
	}

	void VideoRoom::onWebrtcState(bool isActive, const std::string& reason) 
	{
		qDebug() << "Janus says our WebRTC PeerConnection is " << (isActive ? "up" : "down") << " now";
		if (isActive) {
			ConfigBitrateRequest request;
			request.request = "configure";
			request.bitrate = 256000;

			if (auto wreh = _pluginContext->webrtcService.lock()) {
				std::shared_ptr<SendMessageEvent> event = std::make_shared<vi::SendMessageEvent>();
				auto lambda = [](bool success, const std::string& message) {
					qDebug() << "message: " << message.c_str();
				};
				std::shared_ptr<vi::EventCallback> callback = std::make_shared<vi::EventCallback>(lambda);
				event->message = x2struct::X::tojson(request);
				event->callback = callback;
				sendMessage(event);
			}
		}
		unmuteVideo();
	}

	void VideoRoom::onSlowLink(bool uplink, bool lost) {}

	void VideoRoom::onMessage(const EventData& data, const Jsep& jsep)
	{
		qDebug() << " ::: Got a message (publisher).";
		if (!data.xhas("videoroom")) {
			return;
		}
		const auto& event = data.videoroom;
		if (event == "joined") {
			// Publisher/manager created, negotiate WebRTC and attach to existing feeds, if any
			_id = data.id;
			_privateId = data.private_id;
			qDebug() <<"Successfully joined room " << data.room <<  " with ID " << _id;

			// TODO:
			publishOwnStream(true);

			// Any new feed to attach to
			if (data.xhas("publishers")) {
				const auto& publishers = data.publishers;
				qDebug() << "Got a list of available publishers/feeds:";
				for (const auto& pub : publishers) {
					qDebug() << "  >> [" << pub.id << "] " << pub.display.c_str() << " (audio: " << pub.audio_codec.c_str() << ", video: " << pub.video_codec.c_str() << ")";
					// TODO:
					createParticipant(pub.id, pub.display, pub.audio_codec, pub.video_codec);
				}
			}
		}
		else if (event == "destroyed") {
			qWarning() << "The room has been destroyed!";
		}
		else if (event == "event") {
			// Any new feed to attach to
			if (data.xhas("publishers")) {
				const auto& publishers = data.publishers;
				qDebug() << "Got a list of available publishers/feeds:";
				for (const auto& pub : publishers) {
					qDebug() << "  >> [" << pub.id << "] " << pub.display.c_str() << " (audio: " << pub.audio_codec.c_str() << ", video: " << pub.video_codec.c_str() << ")";
					// TODO:
					createParticipant(pub.id, pub.display, pub.audio_codec, pub.video_codec);
				}
			}
		}
		else if (event == "leaving") {
			const auto& leaving = data.leaving;
			// TODO: Figure out the participant and detach it
		}
		else if (event == "unpublished") {
			const auto& unpublished = data.unpublished;
			qDebug() << "Publisher left: " << unpublished;

			// TODO: |unpublished| can be int or string
			if (unpublished == 0) {
				// That's us
				this->hangup(true);
				return;
			}

			// TODO: Figure out the participant and detach it
			//remoteFeed.detach();
		}
		else if (event == "error") {
			if (data.error_code == 426) {
				qDebug() << "No such room";
			}
		}

		if (!jsep.type.empty() && !jsep.sdp.empty()) {
			qDebug() << "Handling SDP as well...";
			// TODO:
			//sfutest.handleRemoteJsep({ jsep: jsep });
			std::shared_ptr<PrepareWebRTCPeerEvent> event = std::make_shared<PrepareWebRTCPeerEvent>();
			auto lambda = [](bool success, const std::string& message) {
				qDebug() << "message: " << message.c_str();
			};
			std::shared_ptr<vi::EventCallback> callback = std::make_shared<vi::EventCallback>(lambda);
			JsepConfig jst;
			jst.type = jsep.type;
			jst.sdp = jsep.sdp;
			event->jsep = jst;
			event->callback = callback;

			handleRemoteJsep(event);

			if (!_pluginContext) {
				return;
			}

			const auto& audio = data.audio_codec;
			if (_pluginContext->webrtcContext->myStream && _pluginContext->webrtcContext->myStream->GetAudioTracks().size() > 0 && audio.empty()) {
				qWarning() << "Our audio stream has been rejected, viewers won't hear us";
			}

			const auto& video = data.video_codec;
			if (_pluginContext->webrtcContext->myStream && _pluginContext->webrtcContext->myStream->GetVideoTracks().size() > 0 && video.empty()) {
				qWarning() << "Our video stream has been rejected, viewers won't see us";
			}
		}
	}

	void VideoRoom::onCreateLocalStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream)
	{
		_listenerProxy->onCreateStream(_id, stream);
	}

	void VideoRoom::onDeleteLocalStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream)
	{
		_listenerProxy->onDeleteStream(_id, stream);
	}

	void VideoRoom::onCreateRemoteStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {}

	void VideoRoom::onDeleteRemoteStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {}

	void VideoRoom::onData(const std::string& data, const std::string& label) {}

	void VideoRoom::onDataOpen(const std::string& label) {}

	void VideoRoom::onCleanup() 
	{
		_pluginContext->webrtcContext->myStream = nullptr;
	}

	void VideoRoom::onDetached() {}

	void VideoRoom::publishOwnStream(bool audioOn)
	{
		auto wself = weak_from_this();
		std::shared_ptr<PrepareWebRTCEvent> event = std::make_shared<PrepareWebRTCEvent>();
		auto callback = std::make_shared<CreateAnswerOfferCallback>([wself, audioOn](bool success, const std::string& reason, const JsepConfig& jsep) {
			auto self = wself.lock();
			if (!self) {
				return;
			}
			if (success) {
				ConfigAudioVideoRequest request;
				request.audio = audioOn;
				request.video = true;
				if (auto wreh = self->pluginContext()->webrtcService.lock()) {
					std::shared_ptr<SendMessageEvent> event = std::make_shared<vi::SendMessageEvent>();
					auto lambda = [](bool success, const std::string& message) {
						qDebug() << "publishOwnStream: " << message.c_str();
					};
					std::shared_ptr<vi::EventCallback> callback = std::make_shared<vi::EventCallback>(lambda);
					event->message = x2struct::X::tojson(request);
					Jsep jp;
					jp.type = jsep.type;
					jp.sdp = jsep.sdp;
					event->jsep = x2struct::X::tojson(jp);
					event->callback = callback;
					self->sendMessage(event);
				}
			}
			else {
				qDebug() << "WebRTC error: " << reason.c_str();
			}
		});
		event->answerOfferCallback = callback;
		MediaConfig media;
		media.audioRecv = true;
		media.videoRecv = true;
		media.audioSend = audioOn;
		media.videoSend = true;
		event->media = media;
		event->simulcast = false;
		event->simulcast2 = false;
		createOffer(event);
	}

	void VideoRoom::unpublishOwnStream()
	{
		UnpublishRequest request;
		if (auto wreh = pluginContext()->webrtcService.lock()) {
			std::shared_ptr<SendMessageEvent> event = std::make_shared<vi::SendMessageEvent>();
			auto lambda = [](bool success, const std::string& message) {
				qDebug() << "message: " << message.c_str();
			};
			std::shared_ptr<vi::EventCallback> callback = std::make_shared<vi::EventCallback>(lambda);
			event->message = x2struct::X::tojson(request);
			event->callback = callback;
			sendMessage(event);
		}
	}

	void VideoRoom::createParticipant(int64_t id, const std::string& displayName, const std::string& audio, const std::string& video)
	{
		auto participant = std::make_shared<Participant>(_pluginContext->plugin, 
														 _pluginContext->opaqueId, 
														 id,
														 _privateId,
														 displayName,
														 _pluginContext->webrtcService.lock());

		participant->attach();
		participant->setListenerProxy(_listenerProxy);
		_participantsMap[id] = participant;
		_listenerProxy->onCreateParticipant(participant);
	}
}
