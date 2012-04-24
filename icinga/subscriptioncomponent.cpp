#include "i2-icinga.h"

using namespace icinga;

string SubscriptionComponent::GetName(void) const
{
	return "subscriptioncomponent";
}

void SubscriptionComponent::Start(void)
{
	m_SubscriptionEndpoint = make_shared<VirtualEndpoint>();
	m_SubscriptionEndpoint->RegisterMethodHandler("message::Subscribe", bind_weak(&SubscriptionComponent::SubscribeMessageHandler, shared_from_this()));
	m_SubscriptionEndpoint->RegisterMethodHandler("message::Provide", bind_weak(&SubscriptionComponent::ProvideMessageHandler, shared_from_this()));
	m_SubscriptionEndpoint->RegisterMethodSource("message::Subscribe");
	m_SubscriptionEndpoint->RegisterMethodSource("message::Provide");

	EndpointManager::Ptr mgr = GetEndpointManager();
	mgr->OnNewEndpoint += bind_weak(&SubscriptionComponent::NewEndpointHandler, shared_from_this());
	mgr->ForeachEndpoint(bind(&SubscriptionComponent::NewEndpointHandler, this, _1));
	mgr->RegisterEndpoint(m_SubscriptionEndpoint);
}

void SubscriptionComponent::Stop(void)
{
	EndpointManager::Ptr mgr = GetEndpointManager();

	if (mgr)
		mgr->UnregisterEndpoint(m_SubscriptionEndpoint);
}

int SubscriptionComponent::SyncSubscription(Endpoint::Ptr target, string type, const NewMethodEventArgs& nmea)
{
	JsonRpcRequest request;
	request.SetVersion("2.0");
	request.SetMethod(type);

	SubscriptionMessage subscriptionMessage;
	subscriptionMessage.SetMethod(nmea.Method);
	request.SetParams(subscriptionMessage);

	GetEndpointManager()->SendUnicastRequest(m_SubscriptionEndpoint, target, request);

	return 0;
}

int SubscriptionComponent::SyncSubscriptions(Endpoint::Ptr target, const NewEndpointEventArgs& neea)
{
	Endpoint::Ptr source = neea.Endpoint;

	if (!source->IsLocal())
		return 0;

	source->ForeachMethodSink(bind(&SubscriptionComponent::SyncSubscription, this, target, "message::Subscribe", _1));
	source->ForeachMethodSource(bind(&SubscriptionComponent::SyncSubscription, this,  target, "message::Provide", _1));

	// TODO: bind to endpoint's events
	//endpoint->OnNewMethodSink...

	return 0;
}

int SubscriptionComponent::NewEndpointHandler(const NewEndpointEventArgs& neea)
{
	if (neea.Endpoint->IsLocal())
		return 0;

	neea.Endpoint->AddAllowedMethodSinkPrefix("message::");
	neea.Endpoint->AddAllowedMethodSourcePrefix("message::");

	neea.Endpoint->RegisterMethodSink("message::Subscribe");
	neea.Endpoint->RegisterMethodSink("message::Provide");

	GetEndpointManager()->ForeachEndpoint(bind(&SubscriptionComponent::SyncSubscriptions, this, neea.Endpoint, _1));

	return 0;
}

int SubscriptionComponent::SubscribeMessageHandler(const NewRequestEventArgs& nrea)
{
	Message params;
	if (!nrea.Request.GetParams(&params))
		return 0;

	SubscriptionMessage subscriptionMessage = params;

	string method;
	if (!subscriptionMessage.GetMethod(&method))
		return 0;

	nrea.Sender->RegisterMethodSink(method);
	return 0;
}

int SubscriptionComponent::ProvideMessageHandler(const NewRequestEventArgs& nrea)
{
	Message params;
	if (!nrea.Request.GetParams(&params))
		return 0;

	SubscriptionMessage subscriptionMessage = params;

	string method;
	if (!subscriptionMessage.GetMethod(&method))
		return 0;

	nrea.Sender->RegisterMethodSource(method);
	return 0;
}