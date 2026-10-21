#include "aaaaa.h"
#include <stdio.h>
#include <string.h>
#include "../devdriv/vmhost/vmci.hpp"

namespace {
	bool ParseResourceId(const char* text, uint32& value) {
		if (!text || !*text) return false;
		uint32 parsed = 0;
		for (const char* cursor = text; *cursor; cursor++) {
			if (*cursor < '0' || *cursor > '9') return false;
			const uint32 digit = uint32(*cursor - '0');
			if (parsed > (VmciInvalidId - digit) / 10u) return false;
			parsed = parsed * 10u + digit;
		}
		if (parsed < VmciFirstDynamicResourceId || parsed == VmciInvalidId) {
			return false;
		}
		value = parsed;
		return true;
	}

	bool ParseEventType(const char* text, uint32& event) {
		if (!text || text[0] < '0' || text[0] > '4' || text[1]) return false;
		event = uint32(text[0] - '0');
		return true;
	}

	bool Exchange(stduint target, VmciMsg type, const void* request,
		stduint request_size, void* response, stduint response_size) {
		CommMsg message = {};
		message.data.address = _IMM(request);
		message.data.length = request_size;
		message.type = _IMM(type);
		syscomm(1, target, &message);

		message = {};
		message.data.address = _IMM(response);
		message.data.length = response_size;
		syscomm(0, target, &message);
		return message.type == _IMM(type) && message.src == target;
	}

	bool QueryStats(stduint driver_tid, VmciStats& stats) {
		stats = {};
		return Exchange(driver_tid, VmciMsg::StatsQuery, nullptr, 0,
			&stats, sizeof(stats)) && stats.version == VmciProtocolVersion &&
			stats.status == int32(VmciStatus::Success);
	}

	bool QueryGuestRpc(stduint driver_tid, VmciGuestRpcInfo& info) {
		info = {};
		return Exchange(driver_tid, VmciMsg::GuestRpcQuery, nullptr, 0,
			&info, sizeof(info)) && info.version == VmciProtocolVersion;
	}

	bool DestroyEndpoint(stduint driver_tid, uint32 resource_id,
		VmciEndpointResult& result) {
		VmciEndpointRequest request = {};
		request.version = VmciProtocolVersion;
		request.resource_id = resource_id;
		result = {};
		return Exchange(driver_tid, VmciMsg::EndpointDestroy,
			&request, sizeof(request), &result, sizeof(result)) &&
			result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success);
	}

	bool CreateEndpoint(stduint driver_tid, uint32 resource_id,
		VmciEndpointResult& result) {
		VmciEndpointRequest request = {};
		request.version = VmciProtocolVersion;
		request.resource_id = resource_id;
		result = {};
		return Exchange(driver_tid, VmciMsg::EndpointCreate,
			&request, sizeof(request), &result, sizeof(result)) &&
			result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success);
	}

	bool CreateDoorbell(stduint driver_tid, uint32 resource_id,
		VmciDoorbellResult& result) {
		VmciDoorbellRequest request = {};
		request.version = VmciProtocolVersion;
		request.resource_id = resource_id;
		result = {};
		return Exchange(driver_tid, VmciMsg::DoorbellCreate,
			&request, sizeof(request), &result, sizeof(result)) &&
			result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success);
	}

	bool DestroyDoorbell(stduint driver_tid, uint32 resource_id,
		VmciDoorbellResult& result) {
		VmciDoorbellRequest request = {};
		request.version = VmciProtocolVersion;
		request.resource_id = resource_id;
		result = {};
		return Exchange(driver_tid, VmciMsg::DoorbellDestroy,
			&request, sizeof(request), &result, sizeof(result)) &&
			result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success);
	}

	bool NotifyDoorbell(stduint driver_tid, VmciHandle handle,
		VmciDoorbellResult& result) {
		VmciDoorbellNotifyRequest request = {};
		request.version = VmciProtocolVersion;
		request.handle = handle;
		result = {};
		return Exchange(driver_tid, VmciMsg::DoorbellNotify,
			&request, sizeof(request), &result, sizeof(result)) &&
			result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success) &&
			result.transport_result >= 0;
	}

	bool SubscribeEvent(stduint driver_tid, uint32 event,
		VmciEventSubscriptionResult& result) {
		VmciEventSubscribeRequest request = {};
		request.version = VmciProtocolVersion;
		request.event = event;
		result = {};
		return Exchange(driver_tid, VmciMsg::EventSubscribe,
			&request, sizeof(request), &result, sizeof(result)) &&
			result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success);
	}

	bool CreateQueuePair(stduint driver_tid, uint32 peer,
		uint32 local_port, uint32 peer_port, uint64 produce_size,
		uint64 consume_size, VmciQueuePairMode mode,
		VmciQueuePairResult& result) {
		VmciQueuePairCreateRequest request = {};
		request.version = VmciProtocolVersion;
		request.resource_id = VmciInvalidId;
		request.peer = peer;
		request.flags = _IMM(VmciQueuePairFlag::None);
		request.produce_size = produce_size;
		request.consume_size = consume_size;
		request.local_port = local_port;
		request.peer_port = peer_port;
		request.mode = _IMM(mode);
		result = {};
		return Exchange(driver_tid, VmciMsg::QueuePairCreate,
			&request, sizeof(request), &result, sizeof(result)) &&
			result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success);
	}

	bool DestroyQueuePair(stduint driver_tid, uint32 resource_id,
		VmciQueuePairResult& result) {
		VmciQueuePairDestroyRequest request = {};
		request.version = VmciProtocolVersion;
		request.resource_id = resource_id;
		result = {};
		return Exchange(driver_tid, VmciMsg::QueuePairDestroy,
			&request, sizeof(request), &result, sizeof(result)) &&
			result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success);
	}

	bool SendQueuePair(stduint driver_tid, uint32 resource_id,
		const void* payload, uint32 payload_size,
		VmciQueuePairIoPacket& result) {
		if (payload_size > VmciIpcPayloadCapacity) return false;
		VmciQueuePairIoPacket request = {};
		request.version = VmciProtocolVersion;
		request.resource_id = resource_id;
		request.payload_size = payload_size;
		if (payload && payload_size) {
			MemCopyN(request.payload, payload, payload_size);
		}
		result = {};
		return Exchange(driver_tid, VmciMsg::QueuePairSend, &request,
			VmciQueuePairIoPacketHeaderSize + payload_size, &result,
			sizeof(result)) && result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success) &&
			result.resource_id == resource_id;
	}

	bool ReceiveQueuePair(stduint driver_tid, uint32 resource_id,
		uint32 capacity, VmciQueuePairIoPacket& result) {
		if (capacity > VmciIpcPayloadCapacity) return false;
		VmciQueuePairIoPacket request = {};
		request.version = VmciProtocolVersion;
		request.resource_id = resource_id;
		request.payload_size = capacity;
		result = {};
		return Exchange(driver_tid, VmciMsg::QueuePairReceive, &request,
			VmciQueuePairIoPacketHeaderSize, &result, sizeof(result)) &&
			result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success) &&
			result.resource_id == resource_id &&
			result.payload_size <= capacity;
	}

	bool UnsubscribeEvent(stduint driver_tid, uint32 subscription_id,
		VmciEventSubscriptionResult& result) {
		VmciEventUnsubscribeRequest request = {};
		request.version = VmciProtocolVersion;
		request.subscription_id = subscription_id;
		result = {};
		return Exchange(driver_tid, VmciMsg::EventUnsubscribe,
			&request, sizeof(request), &result, sizeof(result)) &&
			result.version == VmciProtocolVersion &&
			result.status == int32(VmciStatus::Success);
	}

	bool ReceiveEvent(stduint driver_tid, VmciEventPacket& event) {
		event = {};
		CommMsg message = {};
		message.data.address = _IMM(&event);
		message.data.length = sizeof(event);
		syscomm(0, driver_tid, &message);
		return message.type == _IMM(VmciMsg::EventReceive) &&
			message.src == driver_tid &&
			event.version == VmciProtocolVersion &&
			event.status == int32(VmciStatus::Success) &&
			event.payload_size <= VmciEventPayloadCapacity;
	}

	bool ReceiveDoorbell(stduint driver_tid, VmciDoorbellEvent& event) {
		event = {};
		CommMsg message = {};
		message.data.address = _IMM(&event);
		message.data.length = sizeof(event);
		syscomm(0, driver_tid, &message);
		return message.type == _IMM(VmciMsg::DoorbellReceive) &&
			message.src == driver_tid &&
			event.version == VmciProtocolVersion &&
			event.status == int32(VmciStatus::Success);
	}

	bool ReceiveDatagram(stduint driver_tid, VmciDatagramPacket& packet) {
		packet = {};
		CommMsg message = {};
		message.data.address = _IMM(&packet);
		message.data.length = sizeof(packet);
		syscomm(0, driver_tid, &message);
		return message.type == _IMM(VmciMsg::DatagramReceive) &&
			message.src == driver_tid &&
			packet.version == VmciProtocolVersion &&
			packet.status == int32(VmciStatus::Success);
	}

	bool DiscoverDriver(VmciDriverInfo& info) {
		VmciDriverInfo discovery = {};
		if (!Exchange(Task_Devsman, VmciMsg::DriverLookup, nullptr, 0,
			&discovery, sizeof(discovery)) ||
			discovery.version != VmciProtocolVersion ||
			discovery.status != int32(VmciStatus::Success) ||
			!discovery.driver_tid || discovery.context_id == VmciInvalidId) {
			printf("testvmci: driver unavailable status=%d\n\r", discovery.status);
			return false;
		}

		info = {};
		if (!Exchange(discovery.driver_tid, VmciMsg::GetInfo, nullptr, 0,
			&info, sizeof(info)) || info.version != VmciProtocolVersion ||
			info.status != int32(VmciStatus::Success) ||
			info.driver_tid != discovery.driver_tid ||
			info.context_id != discovery.context_id) {
			printf("testvmci: driver info mismatch\n\r");
			return false;
		}
		return true;
	}

	int TestEndpointLifecycle(const VmciDriverInfo& info) {
		VmciStats before = {};
		if (!QueryStats(info.driver_tid, before)) {
			printf("testvmci: initial stats query failed\n\r");
			return 1;
		}

		VmciEndpointResult endpoint = {};
		if (!CreateEndpoint(info.driver_tid, VmciInvalidId, endpoint) ||
			endpoint.handle.context != info.context_id ||
			endpoint.handle.resource < VmciFirstDynamicResourceId) {
			printf("testvmci: endpoint create failed status=%d\n\r",
				endpoint.status);
			return 1;
		}

		VmciStats active = {};
		if (!QueryStats(info.driver_tid, active) ||
			active.endpoint_count != before.endpoint_count + 1) {
			VmciEndpointResult cleanup = {};
			(void)DestroyEndpoint(info.driver_tid, endpoint.handle.resource, cleanup);
			printf("testvmci: endpoint count did not increase before=%u after=%u\n\r",
				unsigned(before.endpoint_count), unsigned(active.endpoint_count));
			return 1;
		}

		VmciEndpointResult destroyed = {};
		if (!DestroyEndpoint(info.driver_tid, endpoint.handle.resource, destroyed) ||
			destroyed.handle.context != endpoint.handle.context ||
			destroyed.handle.resource != endpoint.handle.resource) {
			printf("testvmci: endpoint destroy failed status=%d\n\r",
				destroyed.status);
			return 1;
		}

		VmciStats after = {};
		if (!QueryStats(info.driver_tid, after) ||
			after.endpoint_count != before.endpoint_count) {
			printf("testvmci: endpoint count did not recover before=%u after=%u\n\r",
				unsigned(before.endpoint_count), unsigned(after.endpoint_count));
			return 1;
		}

		printf("testvmci: ok tid=%u cid=%08x endpoint=%u count=%u/%u/%u payload=%u\n\r",
			unsigned(info.driver_tid), unsigned(info.context_id),
			unsigned(endpoint.handle.resource), unsigned(before.endpoint_count),
			unsigned(active.endpoint_count), unsigned(after.endpoint_count),
			unsigned(info.max_payload));
		return 0;
	}

	int TestDoorbellLifecycle(const VmciDriverInfo& info) {
		if ((info.features & VmciFeatureNotifications) == 0) {
			printf("testvmci: notification capability unavailable\n\r");
			return 1;
		}
		VmciStats before = {};
		if (!QueryStats(info.driver_tid, before)) {
			printf("testvmci: initial stats query failed\n\r");
			return 1;
		}

		VmciDoorbellResult created = {};
		if (!CreateDoorbell(info.driver_tid, VmciInvalidId, created) ||
			created.handle.context != info.context_id ||
			created.handle.resource < VmciFirstDynamicResourceId ||
			created.notify_index >= info.doorbell_capacity) {
			printf("testvmci: doorbell create failed status=%d result=%d\n\r",
				created.status, created.transport_result);
			return 1;
		}

		bool notified = false;
		VmciDoorbellResult notify = {};
		VmciDoorbellEvent event = {};
		do {
			VmciStats active = {};
			if (!QueryStats(info.driver_tid, active) ||
				active.doorbell_count != before.doorbell_count + 1) {
				printf("testvmci: doorbell count did not increase before=%u after=%u\n\r",
					unsigned(before.doorbell_count),
					unsigned(active.doorbell_count));
				break;
			}
			if (!NotifyDoorbell(info.driver_tid, created.handle, notify)) {
				printf("testvmci: doorbell notify failed status=%d result=%d\n\r",
					notify.status, notify.transport_result);
				break;
			}
			printf("testvmci: doorbell sent handle=%08x:%u index=%u, waiting\n\r",
				unsigned(created.handle.context),
				unsigned(created.handle.resource),
				unsigned(created.notify_index));
			if (!ReceiveDoorbell(info.driver_tid, event)) {
				printf("testvmci: doorbell receive failed\n\r");
				break;
			}
			if (event.handle.context != created.handle.context ||
				event.handle.resource != created.handle.resource) {
				printf("testvmci: doorbell mismatch handle=%08x:%u\n\r",
					unsigned(event.handle.context),
					unsigned(event.handle.resource));
				break;
			}
			notified = true;
		} while (false);

		VmciDoorbellResult destroyed = {};
		const bool released = DestroyDoorbell(info.driver_tid,
			created.handle.resource, destroyed);
		if (!released) {
			printf("testvmci: doorbell cleanup failed status=%d result=%d\n\r",
				destroyed.status, destroyed.transport_result);
		}
		if (!notified || !released) return 1;

		VmciStats after = {};
		if (!QueryStats(info.driver_tid, after) ||
			after.doorbell_count != before.doorbell_count ||
			after.doorbell_notify_requests !=
				before.doorbell_notify_requests + 1 ||
			after.doorbell_delivered != before.doorbell_delivered + 1) {
			printf("testvmci: doorbell stats mismatch count=%u/%u notify=%u/%u delivered=%u/%u\n\r",
				unsigned(before.doorbell_count), unsigned(after.doorbell_count),
				unsigned(before.doorbell_notify_requests),
				unsigned(after.doorbell_notify_requests),
				unsigned(before.doorbell_delivered),
				unsigned(after.doorbell_delivered));
			return 1;
		}

		printf("testvmci: doorbell ok handle=%08x:%u index=%u irq=%u delivered=%u\n\r",
			unsigned(created.handle.context),
			unsigned(created.handle.resource), unsigned(created.notify_index),
			unsigned(after.doorbell_interrupts),
			unsigned(after.doorbell_delivered));
		return 0;
	}

	int TestEventLifecycle(const VmciDriverInfo& info) {
		if ((info.features & VmciFeatureEvents) == 0) {
			printf("testvmci: event service unavailable\n\r");
			return 1;
		}
		VmciStats before = {};
		if (!QueryStats(info.driver_tid, before)) {
			printf("testvmci: initial stats query failed\n\r");
			return 1;
		}

		const uint32 event = _IMM(VmciEventType::ContextRemoved);
		VmciEventSubscriptionResult subscribed = {};
		if (!SubscribeEvent(info.driver_tid, event, subscribed) ||
			subscribed.subscription_id == 0 ||
			subscribed.subscription_id == VmciInvalidId ||
			subscribed.event != event) {
			printf("testvmci: event subscribe failed status=%d id=%u\n\r",
				subscribed.status, unsigned(subscribed.subscription_id));
			return 1;
		}

		VmciStats active = {};
		if (!QueryStats(info.driver_tid, active) ||
			active.event_subscription_count !=
				before.event_subscription_count + 1) {
			VmciEventSubscriptionResult cleanup = {};
			(void)UnsubscribeEvent(info.driver_tid,
				subscribed.subscription_id, cleanup);
			printf("testvmci: event count did not increase before=%u after=%u\n\r",
				unsigned(before.event_subscription_count),
				unsigned(active.event_subscription_count));
			return 1;
		}

		VmciEventSubscriptionResult unsubscribed = {};
		if (!UnsubscribeEvent(info.driver_tid, subscribed.subscription_id,
			unsubscribed) || unsubscribed.subscription_id !=
				subscribed.subscription_id || unsubscribed.event != event) {
			printf("testvmci: event unsubscribe failed status=%d id=%u\n\r",
				unsubscribed.status, unsigned(unsubscribed.subscription_id));
			return 1;
		}

		VmciStats after = {};
		if (!QueryStats(info.driver_tid, after) ||
			after.event_subscription_count != before.event_subscription_count) {
			printf("testvmci: event count did not recover before=%u after=%u\n\r",
				unsigned(before.event_subscription_count),
				unsigned(after.event_subscription_count));
			return 1;
		}
		printf("testvmci: event subscription ok id=%u event=%u count=%u/%u/%u capacity=%u\n\r",
			unsigned(subscribed.subscription_id), unsigned(event),
			unsigned(before.event_subscription_count),
			unsigned(active.event_subscription_count),
			unsigned(after.event_subscription_count),
			unsigned(info.event_subscription_capacity));
		return 0;
	}

	bool ExchangeQueuePairEcho(stduint driver_tid, uint32 resource_id,
		const uint8* payload, uint32 payload_size) {
		VmciQueuePairIoPacket sent = {};
		if (!SendQueuePair(driver_tid, resource_id, payload, payload_size,
			sent) || sent.payload_size != payload_size) {
			printf("testvmci: queue pair send failed status=%d bytes=%u/%u\n\r",
				sent.status, unsigned(sent.payload_size), unsigned(payload_size));
			return false;
		}

		uint8 echo_reply[VmciIpcPayloadCapacity] = {};
		uint32 received_total = 0;
		while (received_total < payload_size) {
			VmciQueuePairIoPacket received = {};
			if (!ReceiveQueuePair(driver_tid, resource_id,
				payload_size - received_total, received) ||
				!received.payload_size) {
				printf("testvmci: queue pair echo failed status=%d bytes=%u/%u\n\r",
					received.status, unsigned(received_total),
					unsigned(payload_size));
				return false;
			}
			MemCopyN(echo_reply + received_total, received.payload,
				received.payload_size);
			received_total += received.payload_size;
		}
		if (MemCompare(reinterpret_cast<const char*>(echo_reply),
			reinterpret_cast<const char*>(payload), payload_size) != 0) {
			printf("testvmci: queue pair echo payload mismatch bytes=%u\n\r",
				unsigned(received_total));
			return false;
		}
		return true;
	}

	int TestQueuePairLifecycle(const VmciDriverInfo& info, uint32 host_port,
		bool stress) {
		if ((info.features & VmciFeatureQueuePairs) == 0) {
			printf("testvmci: queue pair service unavailable\n\r");
			return 1;
		}
		constexpr uint64 queue_size = 4096;
		VmciStats before = {};
		if (!QueryStats(info.driver_tid, before)) {
			printf("testvmci: initial stats query failed\n\r");
			return 1;
		}

		VmciQueuePairResult created = {};
		printf("testvmci: connecting stream host=%u:%u queue=%u\n\r",
			unsigned(VmciHostContextId), unsigned(host_port),
			unsigned(queue_size));
		if (!CreateQueuePair(info.driver_tid, VmciHostContextId,
			VmciInvalidId, host_port, queue_size, queue_size,
			VmciQueuePairMode::Host, created) ||
			created.handle.context != info.context_id ||
			created.handle.resource < VmciFirstDynamicResourceId ||
			created.peer != VmciHostContextId ||
			created.mode != _IMM(VmciQueuePairMode::Host) ||
			created.local_port < VmciFirstDynamicResourceId ||
			created.peer_port != host_port ||
			created.produce_size != queue_size ||
			created.consume_size != queue_size ||
			created.page_count != 4 || created.dma_size != 4 * 4096u ||
			created.hypervisor_result < 0) {
			printf("testvmci: queue pair create failed status=%d hypervisor=%d queue=%u local=%u peer=%u:%u pages=%u dma=%u\n\r",
				created.status, created.hypervisor_result,
				unsigned(created.produce_size), unsigned(created.local_port),
				unsigned(created.peer), unsigned(created.peer_port),
				unsigned(created.page_count), unsigned(created.dma_size));
			return 1;
		}

		constexpr uint32 stress_rounds = 8;
		constexpr uint32 stress_chunk_size = 1000;
		uint32 echoed_bytes = 0;
		if (stress) {
			uint8 payload[stress_chunk_size] = {};
			for0(round, stress_rounds) {
				for0(index, stress_chunk_size) {
					payload[index] = uint8((round * 37u + index * 13u) & 0xFFu);
				}
				if (!ExchangeQueuePairEcho(info.driver_tid,
					created.handle.resource, payload, stress_chunk_size)) {
					VmciQueuePairResult cleanup = {};
					(void)DestroyQueuePair(info.driver_tid,
						created.handle.resource, cleanup);
					printf("testvmci: queue pair stress failed round=%u/%u\n\r",
						unsigned(round + 1u), unsigned(stress_rounds));
					return 1;
				}
				echoed_bytes += stress_chunk_size;
			}
		}
		else {
			const uint8 echo_payload[] = "mecocoa-vmci-qp";
			const uint32 echo_size = uint32(sizeof(echo_payload) - 1u);
			if (!ExchangeQueuePairEcho(info.driver_tid,
				created.handle.resource, echo_payload, echo_size)) {
				VmciQueuePairResult cleanup = {};
				(void)DestroyQueuePair(info.driver_tid,
					created.handle.resource, cleanup);
				return 1;
			}
			echoed_bytes = echo_size;
		}
		VmciStats active = {};
		if (!QueryStats(info.driver_tid, active) ||
			active.queue_pair_count != before.queue_pair_count + 1 ||
			active.queue_pair_dma_bytes != before.queue_pair_dma_bytes +
				created.dma_size ||
			active.queue_pair_create_requests !=
				before.queue_pair_create_requests + 1 ||
			active.queue_pair_create_failures !=
				before.queue_pair_create_failures) {
			VmciQueuePairResult cleanup = {};
			(void)DestroyQueuePair(info.driver_tid,
				created.handle.resource, cleanup);
			printf("testvmci: queue pair active stats mismatch count=%u/%u dma=%u/%u\n\r",
				unsigned(before.queue_pair_count),
				unsigned(active.queue_pair_count),
				unsigned(before.queue_pair_dma_bytes),
				unsigned(active.queue_pair_dma_bytes));
			return 1;
		}

		VmciQueuePairResult destroyed = {};
		if (!DestroyQueuePair(info.driver_tid,
			created.handle.resource, destroyed) ||
			destroyed.handle.context != created.handle.context ||
			destroyed.handle.resource != created.handle.resource ||
			destroyed.hypervisor_result < 0) {
			printf("testvmci: queue pair destroy failed status=%d hypervisor=%d\n\r",
				destroyed.status, destroyed.hypervisor_result);
			return 1;
		}

		VmciStats after = {};
		if (!QueryStats(info.driver_tid, after) ||
			after.queue_pair_count != before.queue_pair_count ||
			after.queue_pair_dma_bytes != before.queue_pair_dma_bytes ||
			after.queue_pair_detach_requests !=
				before.queue_pair_detach_requests + 1 ||
			after.queue_pair_detach_failures !=
				before.queue_pair_detach_failures) {
			printf("testvmci: queue pair release stats mismatch count=%u/%u dma=%u/%u\n\r",
				unsigned(before.queue_pair_count),
				unsigned(after.queue_pair_count),
				unsigned(before.queue_pair_dma_bytes),
				unsigned(after.queue_pair_dma_bytes));
			return 1;
		}

		if (stress) {
			printf("testvmci: queue pair stream stress ok handle=%08x:%u local=%u peer=%u:%u bytes=%u rounds=%u ring=%u wrap=verified pages=%u dma=%u result=%d/%d count=%u/%u/%u\n\r",
				unsigned(created.handle.context),
				unsigned(created.handle.resource), unsigned(created.local_port),
				unsigned(created.peer), unsigned(created.peer_port),
				unsigned(echoed_bytes), unsigned(stress_rounds),
				unsigned(queue_size), unsigned(created.page_count),
				unsigned(created.dma_size), created.hypervisor_result,
				destroyed.hypervisor_result,
				unsigned(before.queue_pair_count),
				unsigned(active.queue_pair_count),
				unsigned(after.queue_pair_count));
		}
		else {
			printf("testvmci: queue pair stream echo ok handle=%08x:%u local=%u peer=%u:%u bytes=%u pages=%u dma=%u result=%d/%d count=%u/%u/%u\n\r",
				unsigned(created.handle.context),
				unsigned(created.handle.resource), unsigned(created.local_port),
				unsigned(created.peer), unsigned(created.peer_port),
				unsigned(echoed_bytes), unsigned(created.page_count),
				unsigned(created.dma_size), created.hypervisor_result,
				destroyed.hypervisor_result,
				unsigned(before.queue_pair_count),
				unsigned(active.queue_pair_count),
				unsigned(after.queue_pair_count));
		}
		return 0;
	}

	bool IsGuestRpcQueuePair(const VmciDriverInfo& info,
		const VmciQueuePairResult& result, uint32 peer_port,
		uint64 queue_size) {
		return result.status == int32(VmciStatus::Success) &&
			result.handle.context == info.context_id &&
			result.handle.resource >= VmciFirstDynamicResourceId &&
			result.peer == VmciHypervisorContextId &&
			result.mode == _IMM(VmciQueuePairMode::HypervisorGuestRpc) &&
			result.local_port >= VmciFirstDynamicResourceId &&
			result.peer_port == peer_port &&
			result.produce_size == queue_size &&
			result.consume_size == queue_size &&
			result.page_count == 4 && result.dma_size == 4 * 4096u &&
			result.hypervisor_result >= 0;
	}

	int TestHypervisorVsockTransport(const VmciDriverInfo& info) {
		if ((info.features & VmciFeatureQueuePairs) == 0) {
			printf("testvmci: hypervisor vsock unavailable because queue pairs are disabled\n\r");
			return 1;
		}
		constexpr uint64 queue_size = 4096;
		VmciStats before = {};
		if (!QueryStats(info.driver_tid, before)) {
			printf("testvmci: initial stats query failed\n\r");
			return 1;
		}

		VmciQueuePairResult tclo = {};
		printf("testvmci: connecting hypervisor TCLO=%u:%u queue=%u\n\r",
			unsigned(VmciHypervisorContextId),
			unsigned(VmciGuestRpcTcloPort), unsigned(queue_size));
		if (!CreateQueuePair(info.driver_tid, VmciHypervisorContextId,
			VmciInvalidId, VmciGuestRpcTcloPort, queue_size, queue_size,
			VmciQueuePairMode::HypervisorGuestRpc, tclo) ||
			!IsGuestRpcQueuePair(info, tclo, VmciGuestRpcTcloPort,
				queue_size)) {
			if (tclo.status == int32(VmciStatus::Success) &&
				tclo.handle.resource >= VmciFirstDynamicResourceId) {
				VmciQueuePairResult cleanup = {};
				(void)DestroyQueuePair(info.driver_tid,
					tclo.handle.resource, cleanup);
			}
			printf("testvmci: hypervisor TCLO connect failed status=%d hypervisor=%d local=%u pages=%u dma=%u\n\r",
				tclo.status, tclo.hypervisor_result,
				unsigned(tclo.local_port), unsigned(tclo.page_count),
				unsigned(tclo.dma_size));

			// Workstation may reject TCLO 975 while RPCI 976 still proves that the VMCI Hypervisor vsock transport is operational.
			VmciQueuePairResult rpci_probe = {};
			printf("testvmci: probing hypervisor RPCI=%u:%u local=%u queue=%u\n\r",
				unsigned(VmciHypervisorContextId),
				unsigned(VmciGuestRpcRpciPort),
				unsigned(VmciReservedResourceIdMax),
				unsigned(queue_size));
			const bool rpci_connected = CreateQueuePair(info.driver_tid,
				VmciHypervisorContextId, VmciReservedResourceIdMax,
				VmciGuestRpcRpciPort, queue_size, queue_size,
				VmciQueuePairMode::HypervisorGuestRpc, rpci_probe);
			printf("testvmci: hypervisor RPCI probe %s status=%d hypervisor=%d local=%u pages=%u dma=%u\n\r",
				rpci_connected ? "connected" : "failed",
				rpci_probe.status, rpci_probe.hypervisor_result,
				unsigned(rpci_probe.local_port),
				unsigned(rpci_probe.page_count),
				unsigned(rpci_probe.dma_size));
			if (rpci_connected) {
				VmciQueuePairResult cleanup = {};
				(void)DestroyQueuePair(info.driver_tid,
					rpci_probe.handle.resource, cleanup);
			}
			// Keep the test failed because inbound GuestRPC still needs TCLO or its backdoor fallback.
			return 1;
		}

		VmciQueuePairResult rpci = {};
		printf("testvmci: connecting hypervisor RPCI=%u:%u queue=%u\n\r",
			unsigned(VmciHypervisorContextId),
			unsigned(VmciGuestRpcRpciPort), unsigned(queue_size));
		if (!CreateQueuePair(info.driver_tid, VmciHypervisorContextId,
			VmciInvalidId, VmciGuestRpcRpciPort, queue_size, queue_size,
			VmciQueuePairMode::HypervisorGuestRpc, rpci) ||
			!IsGuestRpcQueuePair(info, rpci, VmciGuestRpcRpciPort,
				queue_size)) {
			if (rpci.status == int32(VmciStatus::Success) &&
				rpci.handle.resource >= VmciFirstDynamicResourceId) {
				VmciQueuePairResult cleanup = {};
				(void)DestroyQueuePair(info.driver_tid,
					rpci.handle.resource, cleanup);
			}
			VmciQueuePairResult cleanup = {};
			(void)DestroyQueuePair(info.driver_tid,
				tclo.handle.resource, cleanup);
			printf("testvmci: hypervisor RPCI connect failed status=%d hypervisor=%d local=%u pages=%u dma=%u\n\r",
				rpci.status, rpci.hypervisor_result,
				unsigned(rpci.local_port), unsigned(rpci.page_count),
				unsigned(rpci.dma_size));
			return 1;
		}

		VmciStats active = {};
		const uint64 expected_dma = uint64(tclo.dma_size) + rpci.dma_size;
		if (!QueryStats(info.driver_tid, active) ||
			active.queue_pair_count != before.queue_pair_count + 2 ||
			active.queue_pair_dma_bytes != before.queue_pair_dma_bytes +
				expected_dma) {
			VmciQueuePairResult cleanup = {};
			(void)DestroyQueuePair(info.driver_tid,
				rpci.handle.resource, cleanup);
			(void)DestroyQueuePair(info.driver_tid,
				tclo.handle.resource, cleanup);
			printf("testvmci: hypervisor vsock active stats mismatch count=%u/%u dma=%u/%u\n\r",
				unsigned(before.queue_pair_count),
				unsigned(active.queue_pair_count),
				unsigned(before.queue_pair_dma_bytes),
				unsigned(active.queue_pair_dma_bytes));
			return 1;
		}

		VmciQueuePairResult rpci_destroyed = {};
		VmciQueuePairResult tclo_destroyed = {};
		const bool rpci_released = DestroyQueuePair(info.driver_tid,
			rpci.handle.resource, rpci_destroyed);
		const bool tclo_released = DestroyQueuePair(info.driver_tid,
			tclo.handle.resource, tclo_destroyed);
		VmciStats after = {};
		if (!rpci_released || !tclo_released ||
			!QueryStats(info.driver_tid, after) ||
			after.queue_pair_count != before.queue_pair_count ||
			after.queue_pair_dma_bytes != before.queue_pair_dma_bytes) {
			printf("testvmci: hypervisor vsock cleanup failed status=%d/%d count=%u/%u dma=%u/%u\n\r",
				rpci_destroyed.status, tclo_destroyed.status,
				unsigned(before.queue_pair_count),
				unsigned(after.queue_pair_count),
				unsigned(before.queue_pair_dma_bytes),
				unsigned(after.queue_pair_dma_bytes));
			return 1;
		}

		printf("testvmci: hypervisor vsock transport ok tclo=%u:%u local=%u rpci=%u:%u local=%u dma=%u count=%u/%u/%u\n\r",
			unsigned(tclo.peer), unsigned(tclo.peer_port),
			unsigned(tclo.local_port), unsigned(rpci.peer),
			unsigned(rpci.peer_port), unsigned(rpci.local_port),
			unsigned(expected_dma), unsigned(before.queue_pair_count),
			unsigned(active.queue_pair_count),
			unsigned(after.queue_pair_count));
		return 0;
	}

	int TestGuestRpcBackdoor(const VmciDriverInfo& driver) {
		VmciGuestRpcInfo info = {};
		if (!QueryGuestRpc(driver.driver_tid, info)) {
			printf("testvmci: GuestRPC query failed\n\r");
			return 1;
		}
		printf("testvmci: GuestRPC backdoor ready=%u channel=%u polls=%u rx=%u replied=%u failed=%u caps=%u resolution=%u %ux%u last-bytes=%u last=%s\n\r",
			unsigned(info.ready), unsigned(info.channel_id),
			unsigned(info.polls), unsigned(info.received),
			unsigned(info.replied), unsigned(info.failures),
			unsigned(info.capability_registrations),
			unsigned(info.resolution_requests),
			unsigned(info.resolution_width),
			unsigned(info.resolution_height),
			unsigned(info.last_request_size), info.last_request);
		const bool advertised = (driver.features &
			VmciFeatureGuestRpcBackdoor) != 0;
		if (!advertised || info.status != int32(VmciStatus::Success) ||
			!info.ready || !info.received || !info.replied ||
			!info.capability_registrations) {
			printf("testvmci: GuestRPC backdoor handshake is incomplete feature=%u status=%d\n\r",
				unsigned(advertised), info.status);
			return 1;
		}
		printf("testvmci: GuestRPC backdoor receive/reply verified\n\r");
		return 0;
	}

	int PrintStats(const VmciDriverInfo& info) {
		VmciStats stats = {};
		if (!QueryStats(info.driver_tid, stats)) {
			printf("testvmci: stats query failed\n\r");
			return 1;
		}

		printf("testvmci: resources endpoints=%u/%u doorbells=%u/%u events=%u/%u\n\r",
			unsigned(stats.endpoint_count), unsigned(stats.endpoint_capacity),
			unsigned(stats.doorbell_count), unsigned(stats.doorbell_capacity),
			unsigned(stats.event_subscription_count),
			unsigned(stats.event_subscription_capacity));
		printf("testvmci: queue-pair count=%u/%u create=%u/%u detach=%u/%u dma=%u\n\r",
			unsigned(stats.queue_pair_count),
			unsigned(stats.queue_pair_capacity),
			unsigned(stats.queue_pair_create_requests),
			unsigned(stats.queue_pair_create_failures),
			unsigned(stats.queue_pair_detach_requests),
			unsigned(stats.queue_pair_detach_failures),
			unsigned(stats.queue_pair_dma_bytes));
		printf("testvmci: datagram tx=%u failed=%u rx=%u delivered=%u unbound=%u malformed=%u ipc-drop=%u\n\r",
			unsigned(stats.transmit_requests),
			unsigned(stats.transmit_failures),
			unsigned(stats.receive_datagrams),
			unsigned(stats.receive_delivered),
			unsigned(stats.receive_unbound),
			unsigned(stats.receive_malformed),
			unsigned(stats.receive_ipc_drops));
		printf("testvmci: doorbell notify=%u failed=%u irq=%u delivered=%u ipc-drop=%u\n\r",
			unsigned(stats.doorbell_notify_requests),
			unsigned(stats.doorbell_notify_failures),
			unsigned(stats.doorbell_interrupts),
			unsigned(stats.doorbell_delivered),
			unsigned(stats.doorbell_ipc_drops));
		printf("testvmci: event datagrams=%u delivered=%u unsubscribed=%u malformed=%u ipc-drop=%u\n\r",
			unsigned(stats.event_datagrams),
			unsigned(stats.event_delivered),
			unsigned(stats.event_unsubscribed),
			unsigned(stats.event_malformed),
			unsigned(stats.event_ipc_drops));
		return 0;
	}

	int WaitForEvent(const VmciDriverInfo& info, uint32 event) {
		if ((info.features & VmciFeatureEvents) == 0) {
			printf("testvmci: event service unavailable\n\r");
			return 1;
		}
		VmciEventSubscriptionResult subscribed = {};
		if (!SubscribeEvent(info.driver_tid, event, subscribed)) {
			printf("testvmci: event subscribe failed status=%d\n\r",
				subscribed.status);
			return 1;
		}
		printf("testvmci: event subscribed id=%u event=%u, waiting\n\r",
			unsigned(subscribed.subscription_id), unsigned(event));

		VmciEventPacket packet = {};
		const bool received = ReceiveEvent(info.driver_tid, packet);
		VmciEventSubscriptionResult unsubscribed = {};
		const bool released = UnsubscribeEvent(info.driver_tid,
			subscribed.subscription_id, unsubscribed);
		if (!received || packet.subscription_id != subscribed.subscription_id ||
			packet.event != event) {
			printf("testvmci: event receive failed id=%u event=%u\n\r",
				unsigned(packet.subscription_id), unsigned(packet.event));
			return 1;
		}
		if (!released) {
			printf("testvmci: event cleanup failed status=%d\n\r",
				unsubscribed.status);
			return 1;
		}

		printf("testvmci: event received id=%u event=%u payload=%u",
			unsigned(packet.subscription_id), unsigned(packet.event),
			unsigned(packet.payload_size));
		if ((event == _IMM(VmciEventType::ContextIdUpdate) ||
			event == _IMM(VmciEventType::ContextRemoved)) &&
			packet.payload_size >= sizeof(VmciEventContextPayload)) {
			VmciEventContextPayload payload = {};
			MemCopyN(&payload, packet.payload, sizeof(payload));
			printf(" context=%08x", unsigned(payload.context_id));
		}
		else if (event >= _IMM(VmciEventType::QueuePairResumed) &&
			event <= _IMM(VmciEventType::QueuePairPeerDetach) &&
			packet.payload_size >= sizeof(VmciEventQueuePairPayload)) {
			VmciEventQueuePairPayload payload = {};
			MemCopyN(&payload, packet.payload, sizeof(payload));
			printf(" handle=%08x:%u peer=%08x",
				unsigned(payload.handle.context),
				unsigned(payload.handle.resource), unsigned(payload.peer_id));
		}
		printf("\n\r");
		return 0;
	}

	int TestHostEcho(const VmciDriverInfo& info, uint32 host_port,
		const char* payload) {
		const stduint payload_size = payload ? StrLength(payload) : 0;
		if (!payload_size || payload_size > info.max_payload ||
			payload_size > VmciIpcPayloadCapacity) {
			printf("testvmci: invalid echo payload size=%u\n\r",
				unsigned(payload_size));
			return 1;
		}

		VmciEndpointResult endpoint = {};
		if (!CreateEndpoint(info.driver_tid, VmciInvalidId, endpoint)) {
			printf("testvmci: endpoint create failed status=%d\n\r",
				endpoint.status);
			return 1;
		}

		static VmciDatagramPacket packet = {};
		VmciDatagramResult sent = {};
		bool echoed = false;
		do {
			packet = {};
			packet.version = VmciProtocolVersion;
			packet.destination.context = VmciHostContextId;
			packet.destination.resource = host_port;
			packet.source = endpoint.handle;
			packet.payload_size = uint32(payload_size);
			MemCopyN(packet.payload, payload, payload_size);
			if (!Exchange(info.driver_tid, VmciMsg::DatagramSend, &packet,
				VmciDatagramPacketHeaderSize + payload_size,
				&sent, sizeof(sent)) || sent.version != VmciProtocolVersion ||
				sent.status != int32(VmciStatus::Success) || sent.result < 0) {
				printf("testvmci: send failed status=%d result=%d\n\r",
					sent.status, sent.result);
				break;
			}

			printf("testvmci: sent %u bytes endpoint=%u host=2:%u, waiting for echo\n\r",
				unsigned(payload_size), unsigned(endpoint.handle.resource),
				unsigned(host_port));
			if (!ReceiveDatagram(info.driver_tid, packet)) {
				printf("testvmci: receive failed\n\r");
				break;
			}
			if (packet.destination.context != endpoint.handle.context ||
				packet.destination.resource != endpoint.handle.resource ||
				packet.source.context != VmciHostContextId ||
				packet.source.resource != host_port ||
				packet.payload_size != payload_size ||
				MemCompare(reinterpret_cast<const char*>(packet.payload),
					payload, payload_size) != 0) {
				printf("testvmci: echo mismatch src=%08x:%u dst=%08x:%u size=%u\n\r",
					unsigned(packet.source.context), unsigned(packet.source.resource),
					unsigned(packet.destination.context),
					unsigned(packet.destination.resource),
					unsigned(packet.payload_size));
				break;
			}
			echoed = true;
		} while (false);

		VmciEndpointResult destroyed = {};
		const bool released = DestroyEndpoint(info.driver_tid,
			endpoint.handle.resource, destroyed);
		if (!released) {
			printf("testvmci: endpoint cleanup failed status=%d\n\r",
				destroyed.status);
		}
		if (!echoed || !released) return 1;

		printf("testvmci: echo ok host=2:%u endpoint=%u bytes=%u payload=%s\n\r",
			unsigned(host_port), unsigned(endpoint.handle.resource),
			unsigned(payload_size), payload);
		return 0;
	}
}

int main(int argc, char** argv) {
	VmciDriverInfo info = {};
	if (!DiscoverDriver(info)) return 1;
	if (argc == 1) return TestEndpointLifecycle(info);
	if (StrCompare(argv[1], "--doorbell") == 0 && argc == 2) {
		return TestDoorbellLifecycle(info);
	}
	if (StrCompare(argv[1], "--stats") == 0 && argc == 2) {
		return PrintStats(info);
	}
	if (StrCompare(argv[1], "--hypervisor-vsock") == 0 && argc == 2) {
		return TestHypervisorVsockTransport(info);
	}
	if (StrCompare(argv[1], "--guest-rpc") == 0 && argc == 2) {
		return TestGuestRpcBackdoor(info);
	}
	const bool queue_pair_test = StrCompare(argv[1], "--queue-pair") == 0;
	const bool queue_pair_stress =
		StrCompare(argv[1], "--queue-pair-stress") == 0;
	if ((queue_pair_test || queue_pair_stress) && argc >= 2 && argc <= 3) {
		uint32 host_port = 7777;
		if (argc == 3 && !ParseResourceId(argv[2], host_port)) {
			printf("testvmci: invalid host port=%s\n\r", argv[2]);
			return 1;
		}
		return TestQueuePairLifecycle(info, host_port, queue_pair_stress);
	}
	if (StrCompare(argv[1], "--event") == 0 && argc >= 2 && argc <= 3) {
		if (argc == 2) return TestEventLifecycle(info);
		uint32 event = 0;
		if (!ParseEventType(argv[2], event)) {
			printf("testvmci: invalid event=%s, expected 0..4\n\r", argv[2]);
			return 1;
		}
		return WaitForEvent(info, event);
	}
	if (StrCompare(argv[1], "--echo") != 0 || argc > 4) {
		printf("usage: testvmci [--stats | --hypervisor-vsock | --guest-rpc | --queue-pair [host-port] | --queue-pair-stress [host-port] | --doorbell | --event [0..4] | --echo [host-port [payload]]]\n\r");
		return 1;
	}

	uint32 host_port = 7777;
	if (argc >= 3) {
		if (!ParseResourceId(argv[2], host_port)) {
			printf("testvmci: invalid host port=%s\n\r", argv[2]);
			return 1;
		}
	}
	return TestHostEcho(info, host_port,
		argc >= 4 ? argv[3] : "mecocoa-vmci");
}
