#ifndef ENDPOINT_H
#define ENDPOINT_H

namespace icinga
{

class EndpointManager;

struct I2_ICINGA_API NewMethodEventArgs : public EventArgs
{
	string Method;
};

class I2_ICINGA_API Endpoint : public Object
{
private:
	set<string> m_MethodSinks;
	set<string> m_MethodSources;

	shared_ptr<EndpointManager> m_EndpointManager;

public:
	typedef shared_ptr<Endpoint> Ptr;
	typedef weak_ptr<Endpoint> WeakPtr;

	shared_ptr<EndpointManager> GetEndpointManager(void) const;
	void SetEndpointManager(shared_ptr<EndpointManager> manager);

	void RegisterMethodSink(string method);
	void UnregisterMethodSink(string method);
	bool IsMethodSink(string method) const;

	void RegisterMethodSource(string method);
	void UnregisterMethodSource(string method);
	bool IsMethodSource(string method) const;

	virtual bool IsLocal(void) const = 0;

	virtual void ProcessRequest(Endpoint::Ptr sender, const JsonRpcRequest& message) = 0;
	virtual void ProcessResponse(Endpoint::Ptr sender, const JsonRpcResponse& message) = 0;

	Event<NewMethodEventArgs> OnNewMethodSink;
	Event<NewMethodEventArgs> OnNewMethodSource;

	void ForeachMethodSink(function<int (const NewMethodEventArgs&)> callback);
	void ForeachMethodSource(function<int (const NewMethodEventArgs&)> callback);
};

}

#endif /* ENDPOINT_H */