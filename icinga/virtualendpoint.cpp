#include "i2-icinga.h"

using namespace icinga;

bool VirtualEndpoint::IsLocal(void) const
{
	return true;
}

void VirtualEndpoint::RegisterMethodHandler(string method, function<int (const NewRequestEventArgs&)> callback)
{
	m_MethodHandlers[method] += callback;

	RegisterMethodSink(method);
}

void VirtualEndpoint::UnregisterMethodHandler(string method, function<int (const NewRequestEventArgs&)> callback)
{
	// TODO: implement
	//m_MethodHandlers[method] -= callback;
	//UnregisterMethodSink(method);

	throw NotImplementedException();
}

void VirtualEndpoint::ProcessRequest(Endpoint::Ptr sender, const JsonRpcRequest& request)
{
	string method;
	if (!request.GetMethod(&method))
		return;

	map<string, Event<NewRequestEventArgs> >::iterator i = m_MethodHandlers.find(method);

	if (i == m_MethodHandlers.end())
		throw InvalidArgumentException();

	NewRequestEventArgs nrea;
	nrea.Source = shared_from_this();
	nrea.Sender = sender;
	nrea.Request = request;
	i->second(nrea);
}

void VirtualEndpoint::ProcessResponse(Endpoint::Ptr sender, const JsonRpcResponse& response)
{
	// TODO: figure out which request this response belongs to and notify the caller
	throw NotImplementedException();
}