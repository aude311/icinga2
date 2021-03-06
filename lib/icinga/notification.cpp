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

#include "icinga/notification.h"
#include "icinga/notificationcommand.h"
#include "icinga/macroprocessor.h"
#include "icinga/service.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include "base/logger_fwd.h"
#include "base/utility.h"
#include "base/convert.h"
#include <boost/tuple/tuple.hpp>
#include <boost/foreach.hpp>
#include <boost/exception/diagnostic_information.hpp>

using namespace icinga;

REGISTER_TYPE(Notification);

boost::signals2::signal<void (const Notification::Ptr&, double, const String&)> Notification::OnNextNotificationChanged;

void Notification::Start(void)
{
	DynamicObject::Start();

	GetService()->AddNotification(GetSelf());
}

void Notification::Stop(void)
{
	DynamicObject::Stop();

	GetService()->RemoveNotification(GetSelf());
}

Service::Ptr Notification::GetService(void) const
{
	Host::Ptr host = Host::GetByName(m_HostName);

	if (!host)
		return Service::Ptr();

	if (m_Service.IsEmpty())
		return host->GetHostCheckService();
	else
		return host->GetServiceByShortName(m_Service);
}

NotificationCommand::Ptr Notification::GetNotificationCommand(void) const
{
	return NotificationCommand::GetByName(m_NotificationCommand);
}

Dictionary::Ptr Notification::GetMacros(void) const
{
	return m_Macros;
}

Array::Ptr Notification::GetExportMacros(void) const
{
	return m_ExportMacros;
}

std::set<User::Ptr> Notification::GetUsers(void) const
{
	std::set<User::Ptr> result;

	Array::Ptr users = m_Users;

	if (users) {
		ObjectLock olock(users);

		BOOST_FOREACH(const String& name, users) {
			User::Ptr user = User::GetByName(name);

			if (!user)
				continue;

			result.insert(user);
		}
	}

	return result;
}

std::set<UserGroup::Ptr> Notification::GetGroups(void) const
{
	std::set<UserGroup::Ptr> result;

	Array::Ptr groups = m_Groups;

	if (groups) {
		ObjectLock olock(groups);

		BOOST_FOREACH(const String& name, groups) {
			UserGroup::Ptr ug = UserGroup::GetByName(name);

			if (!ug)
				continue;

			result.insert(ug);
		}
	}

	return result;
}

Dictionary::Ptr Notification::GetTimes(void) const
{
	return m_Times;
}

unsigned long Notification::GetNotificationTypeFilter(void) const
{
	if (m_NotificationTypeFilter.IsEmpty())
		return ~(unsigned long)0; /* All states. */
	else
		return m_NotificationTypeFilter;
}

unsigned long Notification::GetNotificationStateFilter(void) const
{
	if (m_NotificationStateFilter.IsEmpty())
		return ~(unsigned long)0; /* All states. */
	else
		return m_NotificationStateFilter;
}

double Notification::GetNotificationInterval(void) const
{
	if (m_NotificationInterval.IsEmpty())
		return 300;
	else
		return m_NotificationInterval;
}

TimePeriod::Ptr Notification::GetNotificationPeriod(void) const
{
	return TimePeriod::GetByName(m_NotificationPeriod);
}

double Notification::GetLastNotification(void) const
{
	if (m_LastNotification.IsEmpty())
		return 0;
	else
		return m_LastNotification;
}

/**
 * Sets the timestamp when the last notification was sent.
 */
void Notification::SetLastNotification(double time)
{
	m_LastNotification = time;
}

double Notification::GetNextNotification(void) const
{
	if (m_NextNotification.IsEmpty())
		return 0;
	else
		return m_NextNotification;
}

/**
 * Sets the timestamp when the next periodical notification should be sent.
 * This does not affect notifications that are sent for state changes.
 */
void Notification::SetNextNotification(double time, const String& authority)
{
	m_NextNotification = time;

	Utility::QueueAsyncCallback(bind(boost::ref(OnNextNotificationChanged), GetSelf(), time, authority));
}

long Notification::GetNotificationNumber(void) const
{
	if (m_NotificationNumber.IsEmpty())
		return 0;
	else
		return m_NotificationNumber;
}

void Notification::UpdateNotificationNumber(void)
{
	m_NotificationNumber = m_NotificationNumber + 1;
}

void Notification::ResetNotificationNumber(void)
{
	m_NotificationNumber = 0;
}

String Notification::NotificationTypeToString(NotificationType type)
{
	switch (type) {
		case NotificationDowntimeStart:
			return "DOWNTIMESTART";
		case NotificationDowntimeEnd:
			return "DOWNTIMEEND";
		case NotificationDowntimeRemoved:
			return "DOWNTIMECANCELLED";
		case NotificationCustom:
			return "CUSTOM";
		case NotificationAcknowledgement:
			return "ACKNOWLEDGEMENT";
		case NotificationProblem:
			return "PROBLEM";
		case NotificationRecovery:
			return "RECOVERY";
		case NotificationFlappingStart:
			return "FLAPPINGSTART";
		case NotificationFlappingEnd:
			return "FLAPPINGEND";
		default:
			return "UNKNOWN_NOTIFICATION";
	}
}

void Notification::BeginExecuteNotification(NotificationType type, const Dictionary::Ptr& cr, bool force, const String& author, const String& text)
{
	ASSERT(!OwnsLock());

	if (!force) {
		TimePeriod::Ptr tp = GetNotificationPeriod();

		if (tp && !tp->IsInside(Utility::GetTime())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" + GetName() + "': not in timeperiod");
			return;
		}

		double now = Utility::GetTime();
		Dictionary::Ptr times = GetTimes();
		Service::Ptr service = GetService();

		if (type == NotificationProblem) {
			if (times && times->Contains("begin") && now < service->GetLastHardStateChange() + times->Get("begin")) {
				Log(LogInformation, "icinga", "Not sending notifications for notification object '" + GetName() + "': before escalation range");
				return;
			}

			if (times && times->Contains("end") && now > service->GetLastHardStateChange() + times->Get("end")) {
				Log(LogInformation, "icinga", "Not sending notifications for notification object '" + GetName() + "': after escalation range");
				return;
			}
		}

		unsigned long ftype = 1 << type;

		Log(LogDebug, "icinga", "FType=" + Convert::ToString(ftype) + ", TypeFilter=" + Convert::ToString(GetNotificationTypeFilter()));

		if (!(ftype & GetNotificationTypeFilter())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" + GetName() + "': type filter does not match");
			return;
		}

		unsigned long fstate = 1 << GetService()->GetState();

		if (!(fstate & GetNotificationStateFilter())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" + GetName() + "': state filter does not match");
			return;
		}
	}

	{
		ObjectLock olock(this);

		SetLastNotification(Utility::GetTime());
	}

	std::set<User::Ptr> allUsers;

	std::set<User::Ptr> users = GetUsers();
	std::copy(users.begin(), users.end(), std::inserter(allUsers, allUsers.begin()));

	BOOST_FOREACH(const UserGroup::Ptr& ug, GetGroups()) {
		std::set<User::Ptr> members = ug->GetMembers();
		std::copy(members.begin(), members.end(), std::inserter(allUsers, allUsers.begin()));
	}

	BOOST_FOREACH(const User::Ptr& user, allUsers) {
		Log(LogDebug, "icinga", "Sending notification for user '" + user->GetName() + "'");
		Utility::QueueAsyncCallback(boost::bind(&Notification::ExecuteNotificationHelper, this, type, user, cr, force, author, text));
	}
}

void Notification::ExecuteNotificationHelper(NotificationType type, const User::Ptr& user, const Dictionary::Ptr& cr, bool force, const String& author, const String& text)
{
	ASSERT(!OwnsLock());

	if (!force) {
		TimePeriod::Ptr tp = user->GetNotificationPeriod();

		if (tp && !tp->IsInside(Utility::GetTime())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" +
			    GetName() + " and user '" + user->GetName() + "': user not in timeperiod");
			return;
		}

		unsigned long ftype = 1 << type;

		if (!(ftype & user->GetNotificationTypeFilter())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" +
			    GetName() + " and user '" + user->GetName() + "': type filter does not match");
			return;
		}

		unsigned long fstate = 1 << GetService()->GetState();

		if (!(fstate & user->GetNotificationStateFilter())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" +
			    GetName() + " and user '" + user->GetName() + "': state filter does not match");
			return;
		}
	}

	try {
		GetNotificationCommand()->Execute(GetSelf(), user, cr, type);

		{
			ObjectLock olock(this);
			UpdateNotificationNumber();
			SetLastNotification(Utility::GetTime());
		}

		Service::OnNotificationSentChanged(GetSelf(), user, type, cr, author, text);

		Log(LogInformation, "icinga", "Completed sending notification for service '" + GetService()->GetName() + "'");
	} catch (const std::exception& ex) {
		std::ostringstream msgbuf;
		msgbuf << "Exception occured during notification for service '"
		       << GetService()->GetName() << "': " << boost::diagnostic_information(ex);
		String message = msgbuf.str();

		Log(LogWarning, "icinga", message);
	}
}

bool Notification::ResolveMacro(const String& macro, const Dictionary::Ptr&, String *result) const
{
	Dictionary::Ptr macros = GetMacros();

	if (macros && macros->Contains(macro)) {
		*result = macros->Get(macro);
		return true;
	}

	return false;
}

void Notification::InternalSerialize(const Dictionary::Ptr& bag, int attributeTypes) const
{
	DynamicObject::InternalSerialize(bag, attributeTypes);

	bag->Set("notification_command", m_NotificationCommand);
	bag->Set("notification_interval", m_NotificationInterval);
	bag->Set("notification_period", m_NotificationPeriod);
	bag->Set("last_notification", m_LastNotification);
	bag->Set("next_notification", m_NextNotification);
	bag->Set("notification_number", m_NotificationNumber);
	bag->Set("macros", m_Macros);
	bag->Set("users", m_Users);
	bag->Set("groups", m_Groups);
	bag->Set("times", m_Times);
	bag->Set("notification_type_filter", m_NotificationTypeFilter);
	bag->Set("notification_state_filter", m_NotificationStateFilter);
	bag->Set("host_name", m_HostName);
	bag->Set("export_macros", m_ExportMacros);
	bag->Set("service", m_Service);
}

void Notification::InternalDeserialize(const Dictionary::Ptr& bag, int attributeTypes)
{
	DynamicObject::InternalDeserialize(bag, attributeTypes);

	m_NotificationCommand = bag->Get("notification_command");
	m_NotificationInterval = bag->Get("notification_interval");
	m_NotificationPeriod = bag->Get("notification_period");
	m_LastNotification = bag->Get("last_notification");
	m_NextNotification = bag->Get("next_notification");
	m_NotificationNumber = bag->Get("notification_number");
	m_Macros = bag->Get("macros");
	m_Users = bag->Get("users");
	m_Groups = bag->Get("groups");
	m_Times = bag->Get("times");
	m_NotificationTypeFilter = bag->Get("notification_type_filter");
	m_NotificationStateFilter = bag->Get("notification_state_filter");
	m_HostName = bag->Get("host_name");
	m_ExportMacros = bag->Get("export_macros");
	m_Service = bag->Get("service");
}
