///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2024 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <d3d12.h>
#include <unordered_map>
#include <vector>

//#ifdef _DEBUG
#if false
#define TRANSITION_DEBUG_INFO(RESOURCE, STATE) RESOURCE, STATE, #RESOURCE " " #STATE " " __FILE__ " " TOSTRING(__LINE__)
#define TRANSITION_DEBUG_INFO_NAMED(RESOURCE, STATE, NAME) RESOURCE, STATE, (std::string(#RESOURCE " (") + std::string(NAME) + std::string(") " #STATE " " __FILE__ " " TOSTRING(__LINE__))).c_str()
#else
#define TRANSITION_DEBUG_INFO(RESOURCE, STATE) RESOURCE, STATE, #RESOURCE
#define TRANSITION_DEBUG_INFO_NAMED(RESOURCE, STATE, NAME) RESOURCE, STATE, NAME
#endif

inline const char* D3D12_RESOURCE_STATES_To_String(D3D12_RESOURCE_STATES state)
{
    switch (state)
    {
        case D3D12_RESOURCE_STATE_COMMON: return "COMMON";
        case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER: return "VERTEX_AND_CONSTANT_BUFFER";
        case D3D12_RESOURCE_STATE_INDEX_BUFFER: return "INDEX_BUFFER";
        case D3D12_RESOURCE_STATE_RENDER_TARGET: return "RENDER_TARGET";
        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS: return "UNORDERED_ACCESS";
        case D3D12_RESOURCE_STATE_DEPTH_WRITE: return "DEPTH_WRITE";
        case D3D12_RESOURCE_STATE_DEPTH_READ: return "DEPTH_READ";
        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE: return "NON_PIXEL_SHADER_RESOURCE";
        case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE: return "PIXEL_SHADER_RESOURCE";
        case D3D12_RESOURCE_STATE_STREAM_OUT: return "STREAM_OUT";
        case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT: return "INDIRECT_ARGUMENT";
        case D3D12_RESOURCE_STATE_COPY_DEST: return "COPY_DEST";
        case D3D12_RESOURCE_STATE_COPY_SOURCE: return "COPY_SOURCE";
        case D3D12_RESOURCE_STATE_RESOLVE_DEST: return "RESOLVE_DEST";
        case D3D12_RESOURCE_STATE_RESOLVE_SOURCE: return "RESOLVE_SOURCE";
        case D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE: return "RAYTRACING_ACCELERATION_STRUCTURE";
        case D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE: return "SHADING_RATE_SOURCE";
        case D3D12_RESOURCE_STATE_GENERIC_READ: return "GENERIC_READ";
        case D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE: return "ALL_SHADER_RESOURCE";
        default: return "?";
    }
}

using TransitionLogFn = void (*)(bool actualTransition, const char* format, ...);

class TransitionTracker
{
public:
    // Useful for queueing up multiple transitions that you may not want to do immediately.
	struct Item
	{
		ID3D12Resource* resource = nullptr;
		D3D12_RESOURCE_STATES newState = D3D12_RESOURCE_STATE_COMMON;
		std::string debugText;
		bool isUAVBarrier = false;
	};

	bool IsTracked(ID3D12Resource* resource) const
	{
		return m_trackedResources.count(resource) > 0;
	}

    D3D12_RESOURCE_STATES GetCurrentState(ID3D12Resource* resource) const
    {
        if (!IsTracked(resource))
            return D3D12_RESOURCE_STATE_COMMON;

        auto it = m_trackedResources.find(resource);
        return it->second.currentState;
    }

	void Track(ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState, const char* debugText)
	{
        if (!resource)
            return;

        m_transitionLog(false, "%sTracking %s, state %s(%u). %s", m_transitionLogScope.c_str(), debugText, D3D12_RESOURCE_STATES_To_String(initialState), (unsigned int)initialState, debugText);

		TrackedResource trackedResource;
		trackedResource.currentState = initialState;
		trackedResource.desiredState = initialState;
		trackedResource.name = debugText;
		m_trackedResources[resource] = trackedResource;
	}
	
	void Untrack(ID3D12Resource* resource)
	{
        if (!resource || m_trackedResources.count(resource) == 0)
            return;

        m_transitionLog(false, "%sUntracking %s", m_transitionLogScope.c_str(), m_trackedResources[resource].name.c_str());

		m_trackedResources.erase(resource);
	}

	// Needed for DXR. The acceleration structures are in D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, and need a uav barrier
	void UAVBarrier(ID3D12Resource* resource)
	{
		if (m_trackedResources.count(resource) == 0)
			return;

        m_transitionLog(false, "%sUAV barrier %s", m_transitionLogScope.c_str(), resource, m_trackedResources[resource].name.c_str());

		TrackedResource& trackedResource = m_trackedResources[resource];
		trackedResource.wantsUAVBarrier = true;
	}

	// Needed for dealing with state promotions and decay
	void SetStateWithoutTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES newState)
	{
		if (m_trackedResources.count(resource) == 0)
			return;

        m_transitionLog(false, "%sState without transition %s to %s(%u)", m_transitionLogScope.c_str(), m_trackedResources[resource].name.c_str(), D3D12_RESOURCE_STATES_To_String(newState), (unsigned int)newState);

		TrackedResource& trackedResource = m_trackedResources[resource];

		trackedResource.currentState = newState;
		trackedResource.desiredState = newState;
		trackedResource.wantsUAVBarrier = false;
	}

	void Transition(const std::vector<Item>& transitions)
	{
		for (const Item& item : transitions)
		{
			if (item.isUAVBarrier)
				UAVBarrier(item.resource);
			else
				Transition(item.resource, item.newState, item.debugText.c_str());
		}
	}

	void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES newState, const char* debugText)
	{
		if (m_trackedResources.count(resource) == 0)
			return;

        m_transitionLog(false, "%sTransition %s to %s(%u) %s", m_transitionLogScope.c_str(), m_trackedResources[resource].name.c_str(), D3D12_RESOURCE_STATES_To_String(newState), (unsigned int)newState, debugText);

		TrackedResource& trackedResource = m_trackedResources[resource];

		if (trackedResource.currentState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && newState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			trackedResource.wantsUAVBarrier = true;

		trackedResource.desiredState = newState;
	}

	void Flush(ID3D12GraphicsCommandList* commandList)
	{
		m_barriers.clear();

        bool first = true;

		for (auto& it : m_trackedResources)
		{
			ID3D12Resource* resource = it.first;
			TrackedResource& trackedResource = it.second;

			if (trackedResource.currentState != trackedResource.desiredState)
			{
                if (first)
                {
                    m_transitionLog(true, "%s", m_transitionLogScope.c_str());
                    first = false;
                }

                m_transitionLog(true, "    %s: %s -> %s", trackedResource.name.c_str(), D3D12_RESOURCE_STATES_To_String(trackedResource.currentState), D3D12_RESOURCE_STATES_To_String(trackedResource.desiredState));

				D3D12_RESOURCE_BARRIER barrier;
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				barrier.Transition.pResource = resource;
				barrier.Transition.StateBefore = trackedResource.currentState;
				barrier.Transition.StateAfter = trackedResource.desiredState;
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				m_barriers.push_back(barrier);
			}
			else if (trackedResource.wantsUAVBarrier)
			{
                if (first)
                {
                    m_transitionLog(true, "%s", m_transitionLogScope.c_str());
                    first = false;
                }

                m_transitionLog(true, "    %s: UAV Barrier", trackedResource.name.c_str());

				D3D12_RESOURCE_BARRIER barrier;
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
				barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				barrier.UAV.pResource = resource;
				m_barriers.push_back(barrier);
			}

			trackedResource.currentState = trackedResource.desiredState;
			trackedResource.wantsUAVBarrier = false;
		}

		if (m_barriers.size() > 0)
			commandList->ResourceBarrier((UINT)m_barriers.size(), m_barriers.data());
	}

	bool Empty() const
	{
		return m_trackedResources.empty();
	}

	void Clear()
	{
		m_trackedResources.clear();
	}

    void SetTransitionLog(TransitionLogFn logFn)
    {
        m_transitionLog = logFn;
    }

    void SetTransitionLogScope(const char* scope)
    {
        if (!scope)
            scope = "";
        m_transitionLogScope = scope;
    }

private:

	struct TrackedResource
	{
		D3D12_RESOURCE_STATES currentState;
		D3D12_RESOURCE_STATES desiredState;
		bool wantsUAVBarrier = false;
		std::string name;
	};

	std::unordered_map<ID3D12Resource*, TrackedResource> m_trackedResources;
	std::vector<D3D12_RESOURCE_BARRIER> m_barriers; // a member to minimize allocations

    std::string m_transitionLogScope;
    TransitionLogFn m_transitionLog = [](bool actualTransition, const char* format, ...) {};
};