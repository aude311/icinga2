/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012 Icinga Development Team (http://www.icinga.org/)        *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#ifndef COMPATCOMPONENT_H
#define COMPATCOMPONENT_H

#include "icinga/host.h"
#include "icinga/service.h"
#include "icinga/command.h"
#include "icinga/compatutility.h"
#include "base/dynamicobject.h"
#include "base/objectlock.h"
#include "base/timer.h"
#include "base/utility.h"
#include <boost/thread/thread.hpp>
#include <iostream>

namespace icinga
{

/**
 * @ingroup compat
 */
class CompatComponent : public DynamicObject
{
public:
	DECLARE_PTR_TYPEDEFS(CompatComponent);

protected:
	virtual void Start(void);

	virtual void InternalSerialize(const Dictionary::Ptr& bag, int attributeTypes) const;
	virtual void InternalDeserialize(const Dictionary::Ptr& bag, int attributeTypes);

private:
	String m_StatusPath;
	String m_ObjectsPath;
	String m_CommandPath;

#ifndef _WIN32
	boost::thread m_CommandThread;

	void CommandPipeThread(const String& commandPath);
#endif /* _WIN32 */

	Timer::Ptr m_StatusTimer;

	String GetStatusPath(void) const;
	String GetObjectsPath(void) const;
	String GetCommandPath(void) const;

	void DumpCommand(std::ostream& fp, const Command::Ptr& command);
	void DumpTimePeriod(std::ostream& fp, const TimePeriod::Ptr& tp);
	void DumpDowntimes(std::ostream& fp, const Service::Ptr& owner, CompatObjectType type);
	void DumpComments(std::ostream& fp, const Service::Ptr& owner, CompatObjectType type);
	void DumpHostStatus(std::ostream& fp, const Host::Ptr& host);
	void DumpHostObject(std::ostream& fp, const Host::Ptr& host);

	void DumpServiceStatusAttrs(std::ostream& fp, const Service::Ptr& service, CompatObjectType type);

	template<typename T>
	void DumpNameList(std::ostream& fp, const T& list)
	{
		typename T::const_iterator it;
		bool first = true;
		for (it = list.begin(); it != list.end(); it++) {
			if (!first)
				fp << ",";
			else
				first = false;

			ObjectLock olock(*it);
			fp << (*it)->GetName();
		}
	}

	template<typename T>
	void DumpStringList(std::ostream& fp, const T& list)
	{
		typename T::const_iterator it;
		bool first = true;
		for (it = list.begin(); it != list.end(); it++) {
			if (!first)
				fp << ",";
			else
				first = false;

			fp << *it;
		}
	}

	void DumpServiceStatus(std::ostream& fp, const Service::Ptr& service);
	void DumpServiceObject(std::ostream& fp, const Service::Ptr& service);

	void DumpCustomAttributes(std::ostream& fp, const DynamicObject::Ptr& object);

	void StatusTimerHandler(void);
};

}

#endif /* COMPATCOMPONENT_H */
