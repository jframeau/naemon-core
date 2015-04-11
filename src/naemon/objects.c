#include <string.h>
#include "config.h"
#include "common.h"
#include "objects.h"
#include "objects_common.h"
#include "objects_contact.h"
#include "xodtemplate.h"
#include "logging.h"
#include "globals.h"
#include "nm_alloc.h"


/*
 * These get created in xdata/xodtemplate.c:xodtemplate_register_objects()
 * Escalations are attached to the objects they belong to.
 * Dependencies are attached to the dependent end of the object chain.
 */
dkhash_table *object_hash_tables[NUM_HASHED_OBJECT_TYPES];

host *host_list = NULL;
service *service_list = NULL;
hostgroup *hostgroup_list = NULL;
servicegroup *servicegroup_list = NULL;
contactgroup *contactgroup_list = NULL;
hostescalation *hostescalation_list = NULL;
serviceescalation *serviceescalation_list = NULL;
host **host_ary = NULL;
service **service_ary = NULL;
hostgroup **hostgroup_ary = NULL;
servicegroup **servicegroup_ary = NULL;
contactgroup **contactgroup_ary = NULL;
hostescalation **hostescalation_ary = NULL;
serviceescalation **serviceescalation_ary = NULL;
hostdependency **hostdependency_ary = NULL;
servicedependency **servicedependency_ary = NULL;

int __nagios_object_structure_version = CURRENT_OBJECT_STRUCTURE_VERSION;

const struct flag_map service_flag_map[] = {
	{ OPT_WARNING, 'w', "warning" },
	{ OPT_UNKNOWN, 'u', "unknown" },
	{ OPT_CRITICAL, 'c', "critical" },
	{ OPT_FLAPPING, 'f', "flapping" },
	{ OPT_DOWNTIME, 's', "downtime" },
	{ OPT_OK, 'o', "ok" },
	{ OPT_RECOVERY, 'r', "recovery" },
	{ OPT_PENDING, 'p', "pending" },
	{ 0, 0, NULL },
};

const struct flag_map host_flag_map[] = {
	{ OPT_DOWN, 'd', "down" },
	{ OPT_UNREACHABLE, 'u', "unreachable" },
	{ OPT_FLAPPING, 'f', "flapping" },
	{ OPT_RECOVERY, 'r', "recovery" },
	{ OPT_DOWNTIME, 's', "downtime" },
	{ OPT_PENDING, 'p', "pending" },
	{ 0, 0, NULL },
};

/* Host/Service dependencies are not visible in Nagios CGIs, so we exclude them */
unsigned int host_services_value(host *h)
{
	servicesmember *sm;
	unsigned int ret = 0;
	for (sm = h->services; sm; sm = sm->next) {
		ret += sm->service_ptr->hourly_value;
	}
	return ret;
}

static int cmp_sdep(const void *a_, const void *b_)
{
	const servicedependency *a = *(servicedependency **)a_;
	const servicedependency *b = *(servicedependency **)b_;
	int ret;
	ret = a->master_service_ptr->id - b->master_service_ptr->id;
	return ret ? ret : (int)(a->dependent_service_ptr->id - b->dependent_service_ptr->id);
}

static int cmp_hdep(const void *a_, const void *b_)
{
	const hostdependency *a = *(const hostdependency **)a_;
	const hostdependency *b = *(const hostdependency **)b_;
	int ret;
	ret = a->master_host_ptr->id - b->master_host_ptr->id;
	return ret ? ret : (int)(a->dependent_host_ptr->id - b->dependent_host_ptr->id);
}

static int cmp_serviceesc(const void *a_, const void *b_)
{
	const serviceescalation *a = *(const serviceescalation **)a_;
	const serviceescalation *b = *(const serviceescalation **)b_;
	return a->service_ptr->id - b->service_ptr->id;
}

static int cmp_hostesc(const void *a_, const void *b_)
{
	const hostescalation *a = *(const hostescalation **)a_;
	const hostescalation *b = *(const hostescalation **)b_;
	return a->host_ptr->id - b->host_ptr->id;
}

static void post_process_hosts(void)
{
	unsigned int i, slot = 0;

	for (i = 0; slot < num_objects.hostdependencies && i < num_objects.hosts; i++) {
		objectlist *list;
		host *h = host_ary[i];
		struct hostsmember *pm;

		for (pm = h->parent_hosts; pm; pm = pm->next) {
			struct host *parent;
			parent = find_host(pm->host_name);
			/* may already be resolved */
			if (pm->host_name == parent->name)
				continue;
			free(pm->host_name);
			pm->host_name = parent->name;
		}
		for (list = h->notify_deps; list; list = list->next)
			hostdependency_ary[slot++] = (hostdependency *)list->object_ptr;
		for (list = h->exec_deps; list; list = list->next)
			hostdependency_ary[slot++] = (hostdependency *)list->object_ptr;
	}
}

static void post_process_object_config(void)
{
	objectlist *list;
	unsigned int i, slot;

	if (hostdependency_ary)
		free(hostdependency_ary);
	if (servicedependency_ary)
		free(servicedependency_ary);

	hostdependency_ary = nm_calloc(num_objects.hostdependencies, sizeof(void *));
	servicedependency_ary = nm_calloc(num_objects.servicedependencies, sizeof(void *));

	slot = 0;
	for (i = 0; slot < num_objects.servicedependencies && i < num_objects.services; i++) {
		service *s = service_ary[i];
		for (list = s->notify_deps; list; list = list->next)
			servicedependency_ary[slot++] = (servicedependency *)list->object_ptr;
		for (list = s->exec_deps; list; list = list->next)
			servicedependency_ary[slot++] = (servicedependency *)list->object_ptr;
	}
	timing_point("Done post-processing servicedependencies\n");

	post_process_hosts();
	timing_point("Done post-processing host dependencies\n");

	if (servicedependency_ary)
		qsort(servicedependency_ary, num_objects.servicedependencies, sizeof(servicedependency *), cmp_sdep);
	if (hostdependency_ary)
		qsort(hostdependency_ary, num_objects.hostdependencies, sizeof(hostdependency *), cmp_hdep);
	if (hostescalation_ary)
		qsort(hostescalation_ary, num_objects.hostescalations, sizeof(hostescalation *), cmp_hostesc);
	if (serviceescalation_ary)
		qsort(serviceescalation_ary, num_objects.serviceescalations, sizeof(serviceescalation *), cmp_serviceesc);
	timing_point("Done post-sorting slave objects\n");

	hostgroup_list = hostgroup_ary ? *hostgroup_ary : NULL;
	contactgroup_list = contactgroup_ary ? *contactgroup_ary : NULL;
	servicegroup_list = servicegroup_ary ? *servicegroup_ary : NULL;
	host_list = host_ary ? *host_ary : NULL;
	service_list = service_ary ? *service_ary : NULL;
	hostescalation_list = hostescalation_ary ? *hostescalation_ary : NULL;
	serviceescalation_list = serviceescalation_ary ? *serviceescalation_ary : NULL;
}


/* simple state-name helpers, nifty to have all over the place */
const char *service_state_name(int state)
{
	switch (state) {
	case STATE_OK: return "OK";
	case STATE_WARNING: return "WARNING";
	case STATE_CRITICAL: return "CRITICAL";
	}

	return "UNKNOWN";
}

const char *host_state_name(int state)
{
	switch (state) {
	case STATE_UP: return "UP";
	case STATE_DOWN: return "DOWN";
	case STATE_UNREACHABLE: return "UNREACHABLE";
	}

	return "(unknown)";
}


/******************************************************************/
/******* TOP-LEVEL HOST CONFIGURATION DATA INPUT FUNCTION *********/
/******************************************************************/

/* read all host configuration data from external source */
int read_object_config_data(const char *main_config_file, int options)
{
	int result = OK;

	/* reset object counts */
	memset(&num_objects, 0, sizeof(num_objects));

	/* read in data from all text host config files (template-based) */
	result = xodtemplate_read_config_data(main_config_file, options);
	if (result != OK)
		return ERROR;

	/* handle any remaining config mangling */
	post_process_object_config();
	timing_point("Done post-processing configuration\n");

	return result;
}


int get_host_count(void)
{
       return num_objects.hosts;
}

int get_service_count(void)
{
       return num_objects.services;
}


/******************************************************************/
/**************** OBJECT ADDITION FUNCTIONS ***********************/
/******************************************************************/

static int create_object_table(const char *name, unsigned int elems, unsigned int size, void **ptr)
{
	if (!elems) {
		*ptr = NULL;
		return OK;
	}
	*ptr = nm_calloc(elems, size);
	return OK;
}

#define mktable(name, id) \
	create_object_table(#name, ocount[id], sizeof(name *), (void **)&name##_ary)

/* ocount is an array with NUM_OBJECT_TYPES members */
int create_object_tables(unsigned int *ocount)
{
	int i;

	for (i = 0; i < NUM_HASHED_OBJECT_TYPES; i++) {
		const unsigned int hash_size = ocount[i] * 1.5;
		if (!hash_size)
			continue;
		object_hash_tables[i] = dkhash_create(hash_size);
		if (!object_hash_tables[i]) {
			nm_log(NSLOG_CONFIG_ERROR, "Failed to create hash table with %u entries\n", hash_size);
		}
	}

	/*
	 * errors here will always lead to an early exit, so there's no need
	 * to free() successful allocs when later ones fail
	 */
	if (mktable(host, OBJTYPE_HOST) != OK)
		return ERROR;
	if (mktable(service, OBJTYPE_SERVICE) != OK)
		return ERROR;
	if (mktable(hostgroup, OBJTYPE_HOSTGROUP) != OK)
		return ERROR;
	if (mktable(servicegroup, OBJTYPE_SERVICEGROUP) != OK)
		return ERROR;
	if (mktable(contactgroup, OBJTYPE_CONTACTGROUP) != OK)
		return ERROR;
	if (mktable(hostescalation, OBJTYPE_HOSTESCALATION) != OK)
		return ERROR;
	if (mktable(hostdependency, OBJTYPE_HOSTDEPENDENCY) != OK)
		return ERROR;
	if (mktable(serviceescalation, OBJTYPE_SERVICEESCALATION) != OK)
		return ERROR;
	if (mktable(servicedependency, OBJTYPE_SERVICEDEPENDENCY) != OK)
		return ERROR;

	return OK;
}


/* add a new host definition */
host *add_host(char *name, char *display_name, char *alias, char *address, char *check_period, int initial_state, double check_interval, double retry_interval, int max_attempts, int notification_options, double notification_interval, double first_notification_delay, char *notification_period, int notifications_enabled, char *check_command, int checks_enabled, int accept_passive_checks, char *event_handler, int event_handler_enabled, int flap_detection_enabled, double low_flap_threshold, double high_flap_threshold, int flap_detection_options, int stalking_options, int process_perfdata, int check_freshness, int freshness_threshold, char *notes, char *notes_url, char *action_url, char *icon_image, char *icon_image_alt, char *vrml_image, char *statusmap_image, int x_2d, int y_2d, int have_2d_coords, double x_3d, double y_3d, double z_3d, int have_3d_coords, int should_be_drawn, int retain_status_information, int retain_nonstatus_information, int obsess, unsigned int hourly_value)
{
	host *new_host = NULL;
	timeperiod *check_tp = NULL, *notify_tp = NULL;
	int result = OK;

	/* make sure we have the data we need */
	if (name == NULL || !strcmp(name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host name is NULL\n");
		return NULL;
	}

	if (check_period && !(check_tp = find_timeperiod(check_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate check_period '%s' for host '%s'!\n",
		       check_period, name);
		return NULL;
	}
	if (notification_period && !(notify_tp = find_timeperiod(notification_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate notification_period '%s' for host '%s'!\n",
		       notification_period, name);
		return NULL;
	}
	/* check values */
	if (max_attempts <= 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: max_check_attempts must be a positive integer host '%s'\n", name);
		return NULL;
	}
	if (check_interval < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Invalid check_interval value for host '%s'\n", name);
		return NULL;
	}
	if (notification_interval < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Invalid notification_interval value for host '%s'\n", name);
		return NULL;
	}
	if (first_notification_delay < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Invalid first_notification_delay value for host '%s'\n", name);
		return NULL;
	}
	if (freshness_threshold < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Invalid freshness_threshold value for host '%s'\n", name);
		return NULL;
	}

	new_host = nm_calloc(1, sizeof(*new_host));

	/* assign string vars */
	new_host->name = name;
	new_host->display_name = display_name ? display_name : new_host->name;
	new_host->alias = alias ? alias : new_host->name;
	new_host->address = address ? address : new_host->name;
	new_host->check_period = check_tp ? check_tp->name : NULL;
	new_host->notification_period = notify_tp ? notify_tp->name : NULL;
	new_host->notification_period_ptr = notify_tp;
	new_host->check_period_ptr = check_tp;
	new_host->check_command = check_command;
	new_host->event_handler = event_handler;
	new_host->notes = notes;
	new_host->notes_url = notes_url;
	new_host->action_url = action_url;
	new_host->icon_image = icon_image;
	new_host->icon_image_alt = icon_image_alt;
	new_host->vrml_image = vrml_image;
	new_host->statusmap_image = statusmap_image;

	/* duplicate non-string vars */
	new_host->hourly_value = hourly_value;
	new_host->max_attempts = max_attempts;
	new_host->check_interval = check_interval;
	new_host->retry_interval = retry_interval;
	new_host->notification_interval = notification_interval;
	new_host->first_notification_delay = first_notification_delay;
	new_host->notification_options = notification_options;
	new_host->flap_detection_enabled = (flap_detection_enabled > 0) ? TRUE : FALSE;
	new_host->low_flap_threshold = low_flap_threshold;
	new_host->high_flap_threshold = high_flap_threshold;
	new_host->flap_detection_options = flap_detection_options;
	new_host->stalking_options = stalking_options;
	new_host->process_performance_data = (process_perfdata > 0) ? TRUE : FALSE;
	new_host->check_freshness = (check_freshness > 0) ? TRUE : FALSE;
	new_host->freshness_threshold = freshness_threshold;
	new_host->checks_enabled = (checks_enabled > 0) ? TRUE : FALSE;
	new_host->accept_passive_checks = (accept_passive_checks > 0) ? TRUE : FALSE;
	new_host->event_handler_enabled = (event_handler_enabled > 0) ? TRUE : FALSE;
	new_host->x_2d = x_2d;
	new_host->y_2d = y_2d;
	new_host->have_2d_coords = (have_2d_coords > 0) ? TRUE : FALSE;
	new_host->x_3d = x_3d;
	new_host->y_3d = y_3d;
	new_host->z_3d = z_3d;
	new_host->have_3d_coords = (have_3d_coords > 0) ? TRUE : FALSE;
	new_host->should_be_drawn = (should_be_drawn > 0) ? TRUE : FALSE;
	new_host->obsess = (obsess > 0) ? TRUE : FALSE;
	new_host->retain_status_information = (retain_status_information > 0) ? TRUE : FALSE;
	new_host->retain_nonstatus_information = (retain_nonstatus_information > 0) ? TRUE : FALSE;
	new_host->current_state = initial_state;
	new_host->last_state = initial_state;
	new_host->last_hard_state = initial_state;
	new_host->check_type = CHECK_TYPE_ACTIVE;
	new_host->current_attempt = (initial_state == STATE_UP) ? 1 : max_attempts;
	new_host->state_type = HARD_STATE;
	new_host->acknowledgement_type = ACKNOWLEDGEMENT_NONE;
	new_host->notifications_enabled = (notifications_enabled > 0) ? TRUE : FALSE;
	new_host->check_options = CHECK_OPTION_NONE;

	/* add new host to hash table */
	if (result == OK) {
		result = dkhash_insert(object_hash_tables[OBJTYPE_HOST], new_host->name, NULL, new_host);
		switch (result) {
		case DKHASH_EDUPE:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Host '%s' has already been defined\n", name);
			result = ERROR;
			break;
		case DKHASH_OK:
			result = OK;
			break;
		default:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add host '%s' to hash table\n", name);
			result = ERROR;
			break;
		}
	}

	/* handle errors */
	if (result == ERROR) {
		nm_free(new_host);
		return NULL;
	}

	new_host->id = num_objects.hosts++;
	host_ary[new_host->id] = new_host;
	if (new_host->id)
		host_ary[new_host->id - 1]->next = new_host;
	return new_host;
}


hostsmember *add_parent_host_to_host(host *hst, char *host_name)
{
	hostsmember *new_hostsmember = NULL;
	int result = OK;

	/* make sure we have the data we need */
	if (hst == NULL || host_name == NULL || !strcmp(host_name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host is NULL or parent host name is NULL\n");
		return NULL;
	}

	/* a host cannot be a parent/child of itself */
	if (!strcmp(host_name, hst->name)) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host '%s' cannot be a child/parent of itself\n", hst->name);
		return NULL;
	}

	/* allocate memory */
	new_hostsmember = nm_calloc(1, sizeof(hostsmember));

	/* duplicate string vars (we may not have the host in hash yet) */
	new_hostsmember->host_name = nm_strdup(host_name);

	/* handle errors */
	if (result == ERROR) {
		nm_free(new_hostsmember->host_name);
		nm_free(new_hostsmember);
		return NULL;
	}

	/* add the parent host entry to the host definition */
	new_hostsmember->next = hst->parent_hosts;
	hst->parent_hosts = new_hostsmember;

	return new_hostsmember;
}


servicesmember *add_parent_service_to_service(service *svc, char *host_name, char *description)
{
	servicesmember *sm;

	if (!svc || !host_name || !description || !*host_name || !*description)
		return NULL;

	sm = nm_calloc(1, sizeof(*sm));

	sm->host_name = nm_strdup(host_name);
	sm->service_description = nm_strdup(description);
	sm->next = svc->parents;
	svc->parents = sm;
	return sm;
}


hostsmember *add_child_link_to_host(host *hst, host *child_ptr)
{
	hostsmember *new_hostsmember = NULL;

	/* make sure we have the data we need */
	if (hst == NULL || child_ptr == NULL)
		return NULL;

	/* allocate memory */
	new_hostsmember = nm_malloc(sizeof(hostsmember));

	/* assign values */
	new_hostsmember->host_name = child_ptr->name;
	new_hostsmember->host_ptr = child_ptr;

	/* add the child entry to the host definition */
	new_hostsmember->next = hst->child_hosts;
	hst->child_hosts = new_hostsmember;

	return new_hostsmember;
}


servicesmember *add_service_link_to_host(host *hst, service *service_ptr)
{
	servicesmember *new_servicesmember = NULL;

	/* make sure we have the data we need */
	if (hst == NULL || service_ptr == NULL)
		return NULL;

	/* allocate memory */
	new_servicesmember = nm_calloc(1, sizeof(servicesmember));
	/* assign values */
	new_servicesmember->host_name = service_ptr->host_name;
	new_servicesmember->service_description = service_ptr->description;
	new_servicesmember->service_ptr = service_ptr;
	hst->total_services++;

	/* add the child entry to the host definition */
	new_servicesmember->next = hst->services;
	hst->services = new_servicesmember;
	hst->hourly_value += service_ptr->hourly_value;

	return new_servicesmember;
}


static contactgroupsmember *add_contactgroup_to_object(contactgroupsmember **cg_list, const char *group_name)
{
	contactgroupsmember *cgm;
	contactgroup *cg;

	if (!group_name || !*group_name) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contact name is NULL\n");
		return NULL;
	}
	if (!(cg = find_contactgroup(group_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contactgroup '%s' is not defined anywhere\n", group_name);
		return NULL;
	}
	cgm = nm_malloc(sizeof(*cgm));
	cgm->group_name = cg->group_name;
	cgm->group_ptr = cg;
	cgm->next = *cg_list;
	*cg_list = cgm;

	return cgm;
}


/* add a new contactgroup to a host */
contactgroupsmember *add_contactgroup_to_host(host *hst, char *group_name)
{
	return add_contactgroup_to_object(&hst->contact_groups, group_name);
}


/* adds a contact to a host */
contactsmember *add_contact_to_host(host *hst, char *contact_name)
{

	return add_contact_to_object(&hst->contacts, contact_name);
}


/* adds a custom variable to a host */
customvariablesmember *add_custom_variable_to_host(host *hst, char *varname, char *varvalue)
{

	return add_custom_variable_to_object(&hst->custom_variables, varname, varvalue);
}


/* add a new host group to the list in memory */
hostgroup *add_hostgroup(char *name, char *alias, char *notes, char *notes_url, char *action_url)
{
	hostgroup *new_hostgroup = NULL;
	int result = OK;

	/* make sure we have the data we need */
	if (name == NULL || !strcmp(name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Hostgroup name is NULL\n");
		return NULL;
	}

	new_hostgroup = nm_calloc(1, sizeof(*new_hostgroup));

	/* assign vars */
	new_hostgroup->group_name = name;
	new_hostgroup->alias = alias ? alias : name;
	new_hostgroup->notes = notes;
	new_hostgroup->notes_url = notes_url;
	new_hostgroup->action_url = action_url;

	/* add new host group to hash table */
	if (result == OK) {
		result = dkhash_insert(object_hash_tables[OBJTYPE_HOSTGROUP], new_hostgroup->group_name, NULL, new_hostgroup);
		switch (result) {
		case DKHASH_EDUPE:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Hostgroup '%s' has already been defined\n", name);
			result = ERROR;
			break;
		case DKHASH_OK:
			result = OK;
			break;
		default:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add hostgroup '%s' to hash table\n", name);
			result = ERROR;
			break;
		}
	}

	/* handle errors */
	if (result == ERROR) {
		free(new_hostgroup);
		return NULL;
	}

	new_hostgroup->id = num_objects.hostgroups++;
	hostgroup_ary[new_hostgroup->id] = new_hostgroup;
	if (new_hostgroup->id)
		hostgroup_ary[new_hostgroup->id - 1]->next = new_hostgroup;
	return new_hostgroup;
}


/* add a new host to a host group */
hostsmember *add_host_to_hostgroup(hostgroup *temp_hostgroup, char *host_name)
{
	hostsmember *new_member = NULL;
	hostsmember *last_member = NULL;
	hostsmember *temp_member = NULL;
	struct host *h;

	/* make sure we have the data we need */
	if (temp_hostgroup == NULL || (host_name == NULL || !strcmp(host_name, ""))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Hostgroup or group member is NULL\n");
		return NULL;
	}
	if (!(h = find_host(host_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate host '%s' for hostgroup '%s'\n", host_name, temp_hostgroup->group_name);
		return NULL;
	}

	/* allocate memory for a new member */
	new_member = nm_calloc(1, sizeof(hostsmember));
	/* assign vars */
	new_member->host_name = h->name;
	new_member->host_ptr = h;

	/* add (unsorted) link from the host to its group */
	prepend_object_to_objectlist(&h->hostgroups_ptr, (void *)temp_hostgroup);

	/* add the new member to the member list, sorted by host name */
	if (use_large_installation_tweaks == TRUE) {
		new_member->next = temp_hostgroup->members;
		temp_hostgroup->members = new_member;
		return new_member;
	}
	last_member = temp_hostgroup->members;
	for (temp_member = temp_hostgroup->members; temp_member != NULL; temp_member = temp_member->next) {
		if (strcmp(new_member->host_name, temp_member->host_name) < 0) {
			new_member->next = temp_member;
			if (temp_member == temp_hostgroup->members)
				temp_hostgroup->members = new_member;
			else
				last_member->next = new_member;
			break;
		} else
			last_member = temp_member;
	}
	if (temp_hostgroup->members == NULL) {
		new_member->next = NULL;
		temp_hostgroup->members = new_member;
	} else if (temp_member == NULL) {
		new_member->next = NULL;
		last_member->next = new_member;
	}

	return new_member;
}


/* add a new service group to the list in memory */
servicegroup *add_servicegroup(char *name, char *alias, char *notes, char *notes_url, char *action_url)
{
	servicegroup *new_servicegroup = NULL;
	int result = OK;

	/* make sure we have the data we need */
	if (name == NULL || !strcmp(name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Servicegroup name is NULL\n");
		return NULL;
	}

	new_servicegroup = nm_calloc(1, sizeof(*new_servicegroup));

	/* duplicate vars */
	new_servicegroup->group_name = name;
	new_servicegroup->alias = alias ? alias : name;
	new_servicegroup->notes = notes;
	new_servicegroup->notes_url = notes_url;
	new_servicegroup->action_url = action_url;

	/* add new service group to hash table */
	if (result == OK) {
		result = dkhash_insert(object_hash_tables[OBJTYPE_SERVICEGROUP], new_servicegroup->group_name, NULL, new_servicegroup);
		switch (result) {
		case DKHASH_EDUPE:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Servicegroup '%s' has already been defined\n", name);
			result = ERROR;
			break;
		case DKHASH_OK:
			result = OK;
			break;
		default:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add servicegroup '%s' to hash table\n", name);
			result = ERROR;
			break;
		}
	}

	/* handle errors */
	if (result == ERROR) {
		nm_free(new_servicegroup);
		return NULL;
	}

	new_servicegroup->id = num_objects.servicegroups++;
	servicegroup_ary[new_servicegroup->id] = new_servicegroup;
	if (new_servicegroup->id)
		servicegroup_ary[new_servicegroup->id - 1]->next = new_servicegroup;
	return new_servicegroup;
}


/* add a new service to a service group */
servicesmember *add_service_to_servicegroup(servicegroup *temp_servicegroup, char *host_name, char *svc_description)
{
	servicesmember *new_member = NULL;
	servicesmember *last_member = NULL;
	servicesmember *temp_member = NULL;
	struct service *svc;

	/* make sure we have the data we need */
	if (temp_servicegroup == NULL || (host_name == NULL || !strcmp(host_name, "")) || (svc_description == NULL || !strcmp(svc_description, ""))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Servicegroup or group member is NULL\n");
		return NULL;
	}
	if (!(svc = find_service(host_name, svc_description))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate service '%s' on host '%s' for servicegroup '%s'\n", svc_description, host_name, temp_servicegroup->group_name);
		return NULL;
	}

	/* allocate memory for a new member */
	new_member = nm_calloc(1, sizeof(servicesmember));

	/* assign vars */
	new_member->host_name = svc->host_name;
	new_member->service_description = svc->description;
	new_member->service_ptr = svc;

	/* add (unsorted) link from the service to its groups */
	prepend_object_to_objectlist(&svc->servicegroups_ptr, temp_servicegroup);

	/*
	 * add new member to member list, sorted by host name then
	 * service description, unless we're a large installation, in
	 * which case insertion-sorting will take far too long
	 */
	if (use_large_installation_tweaks == TRUE) {
		new_member->next = temp_servicegroup->members;
		temp_servicegroup->members = new_member;
		return new_member;
	}
	last_member = temp_servicegroup->members;
	for (temp_member = temp_servicegroup->members; temp_member != NULL; temp_member = temp_member->next) {

		if (strcmp(new_member->host_name, temp_member->host_name) < 0) {
			new_member->next = temp_member;
			if (temp_member == temp_servicegroup->members)
				temp_servicegroup->members = new_member;
			else
				last_member->next = new_member;
			break;
		}

		else if (strcmp(new_member->host_name, temp_member->host_name) == 0 && strcmp(new_member->service_description, temp_member->service_description) < 0) {
			new_member->next = temp_member;
			if (temp_member == temp_servicegroup->members)
				temp_servicegroup->members = new_member;
			else
				last_member->next = new_member;
			break;
		}

		else
			last_member = temp_member;
	}
	if (temp_servicegroup->members == NULL) {
		new_member->next = NULL;
		temp_servicegroup->members = new_member;
	} else if (temp_member == NULL) {
		new_member->next = NULL;
		last_member->next = new_member;
	}

	return new_member;
}




/* add a new contact group to the list in memory */
contactgroup *add_contactgroup(char *name, char *alias)
{
	contactgroup *new_contactgroup = NULL;
	int result = OK;

	/* make sure we have the data we need */
	if (name == NULL || !strcmp(name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contactgroup name is NULL\n");
		return NULL;
	}

	new_contactgroup = nm_calloc(1, sizeof(*new_contactgroup));

	/* assign vars */
	new_contactgroup->group_name = name;
	new_contactgroup->alias = alias ? alias : name;

	/* add new contact group to hash table */
	if (result == OK) {
		result = dkhash_insert(object_hash_tables[OBJTYPE_CONTACTGROUP], new_contactgroup->group_name, NULL, new_contactgroup);
		switch (result) {
		case DKHASH_EDUPE:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Contactgroup '%s' has already been defined\n", name);
			result = ERROR;
			break;
		case DKHASH_OK:
			result = OK;
			break;
		default:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add contactgroup '%s' to hash table\n", name);
			result = ERROR;
			break;
		}
	}

	/* handle errors */
	if (result == ERROR) {
		free(new_contactgroup);
		return NULL;
	}

	new_contactgroup->id = num_objects.contactgroups++;
	contactgroup_ary[new_contactgroup->id] = new_contactgroup;
	if (new_contactgroup->id)
		contactgroup_ary[new_contactgroup->id - 1]->next = new_contactgroup;
	return new_contactgroup;
}


/* add a new member to a contact group */
contactsmember *add_contact_to_contactgroup(contactgroup *grp, char *contact_name)
{
	contactsmember *new_contactsmember = NULL;
	struct contact *c;

	/* make sure we have the data we need */
	if (grp == NULL || (contact_name == NULL || !strcmp(contact_name, ""))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contactgroup or contact name is NULL\n");
		return NULL;
	}

	if (!(c = find_contact(contact_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate contact '%s' for contactgroup '%s'\n", contact_name, grp->group_name);
		return NULL;
	}

	/* allocate memory for a new member */
	new_contactsmember = nm_calloc(1, sizeof(contactsmember));

	/* assign vars */
	new_contactsmember->contact_name = c->name;
	new_contactsmember->contact_ptr = c;

	/* add the new member to the head of the member list */
	new_contactsmember->next = grp->members;
	grp->members = new_contactsmember;

	prepend_object_to_objectlist(&c->contactgroups_ptr, (void *)grp);

	return new_contactsmember;
}


/* add a new service to the list in memory */
service *add_service(char *host_name, char *description, char *display_name, char *check_period, int initial_state, int max_attempts, int accept_passive_checks, double check_interval, double retry_interval, double notification_interval, double first_notification_delay, char *notification_period, int notification_options, int notifications_enabled, int is_volatile, char *event_handler, int event_handler_enabled, char *check_command, int checks_enabled, int flap_detection_enabled, double low_flap_threshold, double high_flap_threshold, int flap_detection_options, int stalking_options, int process_perfdata, int check_freshness, int freshness_threshold, char *notes, char *notes_url, char *action_url, char *icon_image, char *icon_image_alt, int retain_status_information, int retain_nonstatus_information, int obsess, unsigned int hourly_value)
{
	host *h;
	timeperiod *cp = NULL, *np = NULL;
	service *new_service = NULL;
	int result = OK;

	/* make sure we have everything we need */
	if (host_name == NULL) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host name not provided for service\n");
		return NULL;
	}
	if (!(h = find_host(host_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Unable to locate host '%s' for service '%s'\n",
		           host_name, description);
		return NULL;
	}
	if (description == NULL || !*description) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Found service on host '%s' with no service description\n", host_name);
		return NULL;
	}
	if (check_command == NULL || !*check_command) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: No check command provided for service '%s' on host '%s'\n", host_name, description);
		return NULL;
	}
	if (notification_period && !(np = find_timeperiod(notification_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: notification_period '%s' for service '%s' on host '%s' could not be found!\n", notification_period, description, host_name);
		return NULL;
	}
	if (check_period && !(cp = find_timeperiod(check_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: check_period '%s' for service '%s' on host '%s' not found!\n",
		       check_period, description, host_name);
		return NULL;
	}

	/* check values */
	if (max_attempts <= 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: max_check_attempts must be a positive integer for service '%s' on host '%s'\n", description, host_name);
		return NULL;
	}
	if (check_interval < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: check_interval must be a non-negative integer for service '%s' on host '%s'\n", description, host_name);
		return NULL;
	}
	if (retry_interval <= 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: retry_interval must be a positive integer for service '%s' on host '%s'\n", description, host_name);
		return NULL;
	}
	if (notification_interval < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: notification_interval must be a non-negative integer for service '%s' on host '%s'\n", description, host_name);
		return NULL;
	}
	if (first_notification_delay < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: first_notification_delay must be a non-negative integer for service '%s' on host '%s'\n", description, host_name);
		return NULL;
	}

	/* allocate memory */
	new_service = nm_calloc(1, sizeof(*new_service));

	/* duplicate vars, but assign what we can */
	new_service->notification_period_ptr = np;
	new_service->check_period_ptr = cp;
	new_service->host_ptr = h;
	new_service->check_period = cp ? cp->name : NULL;
	new_service->notification_period = np ? np->name : NULL;
	new_service->host_name = h->name;
	new_service->description = nm_strdup(description);
	if (display_name) {
		new_service->display_name = nm_strdup(display_name);
	} else {
		new_service->display_name = new_service->description;
	}
	new_service->check_command = nm_strdup(check_command);
	if (event_handler) {
		new_service->event_handler = nm_strdup(event_handler);
	}
	if (notes) {
		new_service->notes = nm_strdup(notes);
	}
	if (notes_url) {
		new_service->notes_url = nm_strdup(notes_url);
	}
	if (action_url) {
		new_service->action_url = nm_strdup(action_url);
	}
	if (icon_image) {
		new_service->icon_image = nm_strdup(icon_image);
	}
	if (icon_image_alt) {
		new_service->icon_image_alt = nm_strdup(icon_image_alt);
	}

	new_service->hourly_value = hourly_value;
	new_service->check_interval = check_interval;
	new_service->retry_interval = retry_interval;
	new_service->max_attempts = max_attempts;
	new_service->notification_interval = notification_interval;
	new_service->first_notification_delay = first_notification_delay;
	new_service->notification_options = notification_options;
	new_service->is_volatile = (is_volatile > 0) ? TRUE : FALSE;
	new_service->flap_detection_enabled = (flap_detection_enabled > 0) ? TRUE : FALSE;
	new_service->low_flap_threshold = low_flap_threshold;
	new_service->high_flap_threshold = high_flap_threshold;
	new_service->flap_detection_options = flap_detection_options;
	new_service->stalking_options = stalking_options;
	new_service->process_performance_data = (process_perfdata > 0) ? TRUE : FALSE;
	new_service->check_freshness = (check_freshness > 0) ? TRUE : FALSE;
	new_service->freshness_threshold = freshness_threshold;
	new_service->accept_passive_checks = (accept_passive_checks > 0) ? TRUE : FALSE;
	new_service->event_handler_enabled = (event_handler_enabled > 0) ? TRUE : FALSE;
	new_service->checks_enabled = (checks_enabled > 0) ? TRUE : FALSE;
	new_service->retain_status_information = (retain_status_information > 0) ? TRUE : FALSE;
	new_service->retain_nonstatus_information = (retain_nonstatus_information > 0) ? TRUE : FALSE;
	new_service->notifications_enabled = (notifications_enabled > 0) ? TRUE : FALSE;
	new_service->obsess = (obsess > 0) ? TRUE : FALSE;
	new_service->acknowledgement_type = ACKNOWLEDGEMENT_NONE;
	new_service->check_type = CHECK_TYPE_ACTIVE;
	new_service->current_attempt = (initial_state == STATE_OK) ? 1 : max_attempts;
	new_service->current_state = initial_state;
	new_service->last_state = initial_state;
	new_service->last_hard_state = initial_state;
	new_service->state_type = HARD_STATE;
	new_service->check_options = CHECK_OPTION_NONE;

	/* add new service to hash table */
	if (result == OK) {
		result = dkhash_insert(object_hash_tables[OBJTYPE_SERVICE], new_service->host_name, new_service->description, new_service);
		switch (result) {
		case DKHASH_EDUPE:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Service '%s' on host '%s' has already been defined\n", description, host_name);
			result = ERROR;
			break;
		case DKHASH_OK:
			result = OK;
			break;
		default:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add service '%s' on host '%s' to hash table\n", description, host_name);
			result = ERROR;
			break;
		}
	}

	/* handle errors */
	if (result == ERROR) {
		nm_free(new_service->perf_data);
		nm_free(new_service->plugin_output);
		nm_free(new_service->long_plugin_output);
		nm_free(new_service->event_handler);
		nm_free(new_service->check_command);
		nm_free(new_service->description);
		if (display_name)
			nm_free(new_service->display_name);
		return NULL;
	}

	add_service_link_to_host(h, new_service);

	new_service->id = num_objects.services++;
	service_ary[new_service->id] = new_service;
	if (new_service->id)
		service_ary[new_service->id - 1]->next = new_service;
	return new_service;
}


/* adds a contact group to a service */
contactgroupsmember *add_contactgroup_to_service(service *svc, char *group_name)
{
	return add_contactgroup_to_object(&svc->contact_groups, group_name);
}


/* adds a contact to a service */
contactsmember *add_contact_to_service(service *svc, char *contact_name)
{

	return add_contact_to_object(&svc->contacts, contact_name);
}


/* adds a custom variable to a service */
customvariablesmember *add_custom_variable_to_service(service *svc, char *varname, char *varvalue)
{

	return add_custom_variable_to_object(&svc->custom_variables, varname, varvalue);
}


/* add a new service escalation to the list in memory */
serviceescalation *add_serviceescalation(char *host_name, char *description, int first_notification, int last_notification, double notification_interval, char *escalation_period, int escalation_options)
{
	serviceescalation *new_serviceescalation = NULL;
	service *svc;
	timeperiod *tp = NULL;

	/* make sure we have the data we need */
	if (host_name == NULL || !*host_name || description == NULL || !*description) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Service escalation host name or description is NULL\n");
		return NULL;
	}
	if (!(svc = find_service(host_name, description))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Service '%s' on host '%s' has an escalation but is not defined anywhere!\n",
		       host_name, description);
		return NULL;
	}
	if (escalation_period && !(tp = find_timeperiod(escalation_period))) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: Escalation period '%s' specified in service escalation for service '%s' on host '%s' is not defined anywhere!\n",
		       escalation_period, description, host_name);
		return NULL ;
	}

	new_serviceescalation = nm_calloc(1, sizeof(*new_serviceescalation));

	if (prepend_object_to_objectlist(&svc->escalation_list, new_serviceescalation) != OK) {
		nm_log(NSLOG_CONFIG_ERROR, "Could not add escalation to service '%s' on host '%s'\n",
		       svc->host_name, svc->description);
		return NULL;
	}

	/* assign vars. object names are immutable, so no need to copy */
	new_serviceescalation->host_name = svc->host_name;
	new_serviceescalation->description = svc->description;
	new_serviceescalation->service_ptr = svc;
	new_serviceescalation->escalation_period_ptr = tp;
	if (tp)
		new_serviceescalation->escalation_period = tp->name;

	new_serviceescalation->first_notification = first_notification;
	new_serviceescalation->last_notification = last_notification;
	new_serviceescalation->notification_interval = (notification_interval <= 0) ? 0 : notification_interval;
	new_serviceescalation->escalation_options = escalation_options;

	new_serviceescalation->id = num_objects.serviceescalations++;
	serviceescalation_ary[new_serviceescalation->id] = new_serviceescalation;
	return new_serviceescalation;
}


/* adds a contact group to a service escalation */
contactgroupsmember *add_contactgroup_to_serviceescalation(serviceescalation *se, char *group_name)
{
	return add_contactgroup_to_object(&se->contact_groups, group_name);
}


/* adds a contact to a service escalation */
contactsmember *add_contact_to_serviceescalation(serviceescalation *se, char *contact_name)
{
	return add_contact_to_object(&se->contacts, contact_name);
}

/* adds a service dependency definition */
servicedependency *add_service_dependency(char *dependent_host_name, char *dependent_service_description, char *host_name, char *service_description, int dependency_type, int inherits_parent, int failure_options, char *dependency_period)
{
	servicedependency *new_servicedependency = NULL;
	service *parent, *child;
	timeperiod *tp = NULL;
	int result;
	size_t sdep_size = sizeof(*new_servicedependency);

	/* make sure we have what we need */
	parent = find_service(host_name, service_description);
	if (!parent) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Master service '%s' on host '%s' is not defined anywhere!\n",
		       service_description, host_name);
		return NULL;
	}
	child = find_service(dependent_host_name, dependent_service_description);
	if (!child) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Dependent service '%s' on host '%s' is not defined anywhere!\n",
		       dependent_service_description, dependent_host_name);
		return NULL;
	}
	if (dependency_period && !(tp = find_timeperiod(dependency_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate timeperiod '%s' for dependency from service '%s' on host '%s' to service '%s' on host '%s'\n",
		       dependency_period, dependent_service_description, dependent_host_name, service_description, host_name);
		return NULL;
	}

	/* allocate memory for a new service dependency entry */
	new_servicedependency = nm_calloc(1, sizeof(*new_servicedependency));

	new_servicedependency->dependent_service_ptr = child;
	new_servicedependency->master_service_ptr = parent;
	new_servicedependency->dependency_period_ptr = tp;

	/* assign vars. object names are immutable, so no need to copy */
	new_servicedependency->dependent_host_name = child->host_name;
	new_servicedependency->dependent_service_description = child->description;
	new_servicedependency->host_name = parent->host_name;
	new_servicedependency->service_description = parent->description;
	if (tp)
		new_servicedependency->dependency_period = tp->name;

	new_servicedependency->dependency_type = (dependency_type == EXECUTION_DEPENDENCY) ? EXECUTION_DEPENDENCY : NOTIFICATION_DEPENDENCY;
	new_servicedependency->inherits_parent = (inherits_parent > 0) ? TRUE : FALSE;
	new_servicedependency->failure_options = failure_options;

	/*
	 * add new service dependency to its respective services.
	 * Ordering doesn't matter here as we'll have to check them
	 * all anyway. We avoid adding dupes though, since we can
	 * apparently get zillion's and zillion's of them.
	 */
	if (dependency_type == NOTIFICATION_DEPENDENCY)
		result = prepend_unique_object_to_objectlist_ptr(&child->notify_deps, new_servicedependency, &compare_objects, &sdep_size);
	else
		result = prepend_unique_object_to_objectlist_ptr(&child->exec_deps, new_servicedependency, &compare_objects, &sdep_size);

	if (result != OK) {
		free(new_servicedependency);
		/* hack to avoid caller bombing out */
		return result == OBJECTLIST_DUPE ? (void *)1 : NULL;
	}

	num_objects.servicedependencies++;
	return new_servicedependency;
}


/* adds a host dependency definition */
hostdependency *add_host_dependency(char *dependent_host_name, char *host_name, int dependency_type, int inherits_parent, int failure_options, char *dependency_period)
{
	hostdependency *new_hostdependency = NULL;
	host *parent, *child;
	timeperiod *tp = NULL;
	int result;
	size_t hdep_size = sizeof(*new_hostdependency);

	/* make sure we have what we need */
	parent = find_host(host_name);
	if (!parent) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Master host '%s' in hostdependency from '%s' to '%s' is not defined anywhere!\n",
		       host_name, dependent_host_name, host_name);
		return NULL;
	}
	child = find_host(dependent_host_name);
	if (!child) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Dependent host '%s' in hostdependency from '%s' to '%s' is not defined anywhere!\n",
		       dependent_host_name, dependent_host_name, host_name);
		return NULL;
	}
	if (dependency_period && !(tp = find_timeperiod(dependency_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Unable to locate dependency_period '%s' for %s->%s host dependency\n",
		       dependency_period, parent->name, child->name);
		return NULL ;
	}

	new_hostdependency = nm_calloc(1, sizeof(*new_hostdependency));
	new_hostdependency->dependent_host_ptr = child;
	new_hostdependency->master_host_ptr = parent;
	new_hostdependency->dependency_period_ptr = tp;

	/* assign vars. Objects are immutable, so no need to copy */
	new_hostdependency->dependent_host_name = child->name;
	new_hostdependency->host_name = parent->name;
	if (tp)
		new_hostdependency->dependency_period = tp->name;

	new_hostdependency->dependency_type = (dependency_type == EXECUTION_DEPENDENCY) ? EXECUTION_DEPENDENCY : NOTIFICATION_DEPENDENCY;
	new_hostdependency->inherits_parent = (inherits_parent > 0) ? TRUE : FALSE;
	new_hostdependency->failure_options = failure_options;

	if (dependency_type == NOTIFICATION_DEPENDENCY)
		result = prepend_unique_object_to_objectlist_ptr(&child->notify_deps, new_hostdependency, *compare_objects, &hdep_size);
	else
		result = prepend_unique_object_to_objectlist_ptr(&child->exec_deps, new_hostdependency, *compare_objects, &hdep_size);

	if (result != OK) {
		free(new_hostdependency);
		/* hack to avoid caller bombing out */
		return result == OBJECTLIST_DUPE ? (void *)1 : NULL;
	}

	num_objects.hostdependencies++;
	return new_hostdependency;
}


/* add a new host escalation to the list in memory */
hostescalation *add_hostescalation(char *host_name, int first_notification, int last_notification, double notification_interval, char *escalation_period, int escalation_options)
{
	hostescalation *new_hostescalation = NULL;
	host *h;
	timeperiod *tp = NULL;

	/* make sure we have the data we need */
	if (host_name == NULL || !*host_name) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host escalation host name is NULL\n");
		return NULL;
	}
	if (!(h = find_host(host_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host '%s' has an escalation, but is not defined anywhere!\n", host_name);
		return NULL;
	}
	if (escalation_period && !(tp = find_timeperiod(escalation_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Unable to locate timeperiod '%s' for hostescalation '%s'\n",
		       escalation_period, host_name);
		return NULL;
	}

	new_hostescalation = nm_calloc(1, sizeof(*new_hostescalation));

	/* add the escalation to its host */
	if (prepend_object_to_objectlist(&h->escalation_list, new_hostescalation) != OK) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add hostescalation to host '%s'\n", host_name);
		free(new_hostescalation);
		return NULL;
	}

	/* assign vars. Object names are immutable, so no need to copy */
	new_hostescalation->host_name = h->name;
	new_hostescalation->host_ptr = h;
	new_hostescalation->escalation_period = tp ? tp->name : NULL;
	new_hostescalation->escalation_period_ptr = tp;
	new_hostescalation->first_notification = first_notification;
	new_hostescalation->last_notification = last_notification;
	new_hostescalation->notification_interval = (notification_interval <= 0) ? 0 : notification_interval;
	new_hostescalation->escalation_options = escalation_options;

	new_hostescalation->id = num_objects.hostescalations++;
	hostescalation_ary[new_hostescalation->id] = new_hostescalation;
	return new_hostescalation;
}


/* adds a contact group to a host escalation */
contactgroupsmember *add_contactgroup_to_hostescalation(hostescalation *he, char *group_name)
{
	return add_contactgroup_to_object(&he->contact_groups, group_name);
}


/* adds a contact to a host escalation */
contactsmember *add_contact_to_hostescalation(hostescalation *he, char *contact_name)
{

	return add_contact_to_object(&he->contacts, contact_name);
}


/******************************************************************/
/******************** OBJECT SEARCH FUNCTIONS *********************/
/******************************************************************/

host *find_host(const char *name)
{
	return dkhash_get(object_hash_tables[OBJTYPE_HOST], name, NULL);
}

hostgroup *find_hostgroup(const char *name)
{
	return dkhash_get(object_hash_tables[OBJTYPE_HOSTGROUP], name, NULL);
}

servicegroup *find_servicegroup(const char *name)
{
	return dkhash_get(object_hash_tables[OBJTYPE_SERVICEGROUP], name, NULL);
}

contactgroup *find_contactgroup(const char *name)
{
	return dkhash_get(object_hash_tables[OBJTYPE_CONTACTGROUP], name, NULL);
}

service *find_service(const char *host_name, const char *svc_desc)
{
	return dkhash_get(object_hash_tables[OBJTYPE_SERVICE], host_name, svc_desc);
}


/******************************************************************/
/********************* OBJECT QUERY FUNCTIONS *********************/
/******************************************************************/

/* determines whether or not a specific host is an immediate child of another host */
int is_host_immediate_child_of_host(host *parent_host, host *child_host)
{
	hostsmember *temp_hostsmember = NULL;

	/* not enough data */
	if (child_host == NULL)
		return FALSE;

	/* root/top-level hosts */
	if (parent_host == NULL) {
		if (child_host->parent_hosts == NULL)
			return TRUE;
	}

	/* mid-level/bottom hosts */
	else {

		for (temp_hostsmember = child_host->parent_hosts; temp_hostsmember != NULL; temp_hostsmember = temp_hostsmember->next) {
			if (temp_hostsmember->host_ptr == parent_host)
				return TRUE;
		}
	}

	return FALSE;
}


/* determines whether or not a specific host is an immediate parent of another host */
int is_host_immediate_parent_of_host(host *child_host, host *parent_host)
{

	if (is_host_immediate_child_of_host(parent_host, child_host) == TRUE)
		return TRUE;

	return FALSE;
}

/*  tests whether a host is a member of a particular hostgroup */
/* NOTE: This function is only used by external modules */
int is_host_member_of_hostgroup(hostgroup *group, host *hst)
{
	hostsmember *temp_hostsmember = NULL;

	if (group == NULL || hst == NULL)
		return FALSE;

	for (temp_hostsmember = group->members; temp_hostsmember != NULL; temp_hostsmember = temp_hostsmember->next) {
		if (temp_hostsmember->host_ptr == hst)
			return TRUE;
	}

	return FALSE;
}


/*  tests whether a host is a member of a particular servicegroup */
/* NOTE: This function is only used by external modules (mod_gearman, f.e) */
int is_host_member_of_servicegroup(servicegroup *group, host *hst)
{
	servicesmember *temp_servicesmember = NULL;

	if (group == NULL || hst == NULL)
		return FALSE;

	for (temp_servicesmember = group->members; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {
		if (temp_servicesmember->service_ptr != NULL && temp_servicesmember->service_ptr->host_ptr == hst)
			return TRUE;
	}

	return FALSE;
}


/*  tests whether a service is a member of a particular servicegroup */
/* NOTE: This function is only used by external modules (mod_gearman, f.e) */
int is_service_member_of_servicegroup(servicegroup *group, service *svc)
{
	servicesmember *temp_servicesmember = NULL;

	if (group == NULL || svc == NULL)
		return FALSE;

	for (temp_servicesmember = group->members; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {
		if (temp_servicesmember->service_ptr == svc)
			return TRUE;
	}

	return FALSE;
}


/*
 * tests whether a contact is a member of a particular contactgroup.
 * This function is used by external modules, such as Livestatus
 */
int is_contact_member_of_contactgroup(contactgroup *group, contact *cntct)
{
	contactsmember *member;

	if (!group || !cntct)
		return FALSE;

	/* search all contacts in this contact group */
	for (member = group->members; member; member = member->next) {
		if (member->contact_ptr == cntct)
			return TRUE;
	}

	return FALSE;
}


/*  tests whether a contact is a contact for a particular host */
int is_contact_for_host(host *hst, contact *cntct)
{
	contactsmember *temp_contactsmember = NULL;
	contactgroupsmember *temp_contactgroupsmember = NULL;
	contactgroup *temp_contactgroup = NULL;

	if (hst == NULL || cntct == NULL) {
		return FALSE;
	}

	/* search all individual contacts of this host */
	for (temp_contactsmember = hst->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
		if (temp_contactsmember->contact_ptr == cntct)
			return TRUE;
	}

	/* search all contactgroups of this host */
	for (temp_contactgroupsmember = hst->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
		temp_contactgroup = temp_contactgroupsmember->group_ptr;
		if (is_contact_member_of_contactgroup(temp_contactgroup, cntct))
			return TRUE;
	}

	return FALSE;
}


/* tests whether or not a contact is an escalated contact for a particular host */
int is_escalated_contact_for_host(host *hst, contact *cntct)
{
	contactsmember *temp_contactsmember = NULL;
	hostescalation *temp_hostescalation = NULL;
	contactgroupsmember *temp_contactgroupsmember = NULL;
	contactgroup *temp_contactgroup = NULL;
	objectlist *list;

	/* search all host escalations */
	for (list = hst->escalation_list; list; list = list->next) {
		temp_hostescalation = (hostescalation *)list->object_ptr;

		/* search all contacts of this host escalation */
		for (temp_contactsmember = temp_hostescalation->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
			if (temp_contactsmember->contact_ptr == cntct)
				return TRUE;
		}

		/* search all contactgroups of this host escalation */
		for (temp_contactgroupsmember = temp_hostescalation->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
			temp_contactgroup = temp_contactgroupsmember->group_ptr;
			if (is_contact_member_of_contactgroup(temp_contactgroup, cntct))
				return TRUE;
		}
	}

	return FALSE;
}


/*  tests whether a contact is a contact for a particular service */
int is_contact_for_service(service *svc, contact *cntct)
{
	contactsmember *temp_contactsmember = NULL;
	contactgroupsmember *temp_contactgroupsmember = NULL;
	contactgroup *temp_contactgroup = NULL;

	if (svc == NULL || cntct == NULL)
		return FALSE;

	/* search all individual contacts of this service */
	for (temp_contactsmember = svc->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
		if (temp_contactsmember->contact_ptr == cntct)
			return TRUE;
	}

	/* search all contactgroups of this service */
	for (temp_contactgroupsmember = svc->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
		temp_contactgroup = temp_contactgroupsmember->group_ptr;
		if (is_contact_member_of_contactgroup(temp_contactgroup, cntct))
			return TRUE;

	}

	return FALSE;
}


/* tests whether or not a contact is an escalated contact for a particular service */
int is_escalated_contact_for_service(service *svc, contact *cntct)
{
	serviceescalation *temp_serviceescalation = NULL;
	contactsmember *temp_contactsmember = NULL;
	contactgroupsmember *temp_contactgroupsmember = NULL;
	contactgroup *temp_contactgroup = NULL;
	objectlist *list;

	/* search all the service escalations */
	for (list = svc->escalation_list; list; list = list->next) {
		temp_serviceescalation = (serviceescalation *)list->object_ptr;

		/* search all contacts of this service escalation */
		for (temp_contactsmember = temp_serviceescalation->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
			if (temp_contactsmember->contact_ptr == cntct)
				return TRUE;
		}

		/* search all contactgroups of this service escalation */
		for (temp_contactgroupsmember = temp_serviceescalation->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
			temp_contactgroup = temp_contactgroupsmember->group_ptr;
			if (is_contact_member_of_contactgroup(temp_contactgroup, cntct))
				return TRUE;
		}
	}

	return FALSE;
}


/******************************************************************/
/******************* OBJECT DELETION FUNCTIONS ********************/
/******************************************************************/

static void destroy_hostsmember(struct hostsmember *cur)
{
	struct hostsmember *next;

	while (cur) {
		next = cur->next;
		nm_free(cur);
		cur = next;
	}
}

static void destroy_customvars(struct customvariablesmember *cur)
{
	struct customvariablesmember *next;

	while (cur) {
		next = cur->next;
		nm_free(cur->variable_name);
		nm_free(cur->variable_value);
		nm_free(cur);
		cur = next;
	}
}


static void destroy_contactsmember(struct contactsmember *cur)
{
	struct contactsmember *next;

	while (cur) {
		next = cur->next;
		nm_free(cur);
		cur = next;
	}
}


static void destroy_contactgroupsmember(struct contactgroupsmember *cur)
{
	struct contactgroupsmember *next;

	for (; cur; cur = next) {
		next = cur->next;
		nm_free(cur);
	}
}


static void destroy_servicesmember(struct servicesmember *cur)
{
	struct servicesmember *next;
	while (cur) {
		next = cur->next;
		nm_free(cur);
		cur = next;
	}
}


static void destroy_host(struct host *this_host)
{
	destroy_hostsmember(this_host->parent_hosts);
	destroy_hostsmember(this_host->child_hosts);
	destroy_servicesmember(this_host->services);
	destroy_contactgroupsmember(this_host->contact_groups);
	destroy_contactsmember(this_host->contacts);
	destroy_customvars(this_host->custom_variables);

	if (this_host->display_name != this_host->name)
		nm_free(this_host->display_name);
	if (this_host->alias != this_host->name)
		nm_free(this_host->alias);
	if (this_host->address != this_host->name)
		nm_free(this_host->address);
	nm_free(this_host->name);
	nm_free(this_host->plugin_output);
	nm_free(this_host->long_plugin_output);
	nm_free(this_host->perf_data);
	free_objectlist(&this_host->hostgroups_ptr);
	free_objectlist(&this_host->notify_deps);
	free_objectlist(&this_host->exec_deps);
	free_objectlist(&this_host->escalation_list);
	nm_free(this_host->check_command);
	nm_free(this_host->event_handler);
	nm_free(this_host->notes);
	nm_free(this_host->notes_url);
	nm_free(this_host->action_url);
	nm_free(this_host->icon_image);
	nm_free(this_host->icon_image_alt);
	nm_free(this_host->vrml_image);
	nm_free(this_host->statusmap_image);
	nm_free(this_host);
}

static void destroy_hostgroup(struct hostgroup *this_hostgroup)
{
	struct hostsmember *this_hostsmember, *next_hostsmember;

	this_hostsmember = this_hostgroup->members;
	while (this_hostsmember != NULL) {
		next_hostsmember = this_hostsmember->next;
		nm_free(this_hostsmember);
		this_hostsmember = next_hostsmember;
	}

	if (this_hostgroup->alias != this_hostgroup->group_name)
		nm_free(this_hostgroup->alias);
	nm_free(this_hostgroup->group_name);
	nm_free(this_hostgroup->notes);
	nm_free(this_hostgroup->notes_url);
	nm_free(this_hostgroup->action_url);
	nm_free(this_hostgroup);
}

static void destroy_servicegroup(struct servicegroup *this_servicegroup)
{
	struct servicesmember *this_servicesmember, *next_servicesmember;

	this_servicesmember = this_servicegroup->members;
	while (this_servicesmember != NULL) {
		next_servicesmember = this_servicesmember->next;
		nm_free(this_servicesmember);
		this_servicesmember = next_servicesmember;
	}

	if (this_servicegroup->alias != this_servicegroup->group_name)
		nm_free(this_servicegroup->alias);
	nm_free(this_servicegroup->group_name);
	nm_free(this_servicegroup->notes);
	nm_free(this_servicegroup->notes_url);
	nm_free(this_servicegroup->action_url);
	nm_free(this_servicegroup);
}

static void destroy_contactgroup(struct contactgroup *this_contactgroup)
{
	struct contactsmember *this_contactsmember, *next_contactsmember;

	/* free memory for the group members */
	this_contactsmember = this_contactgroup->members;
	while (this_contactsmember != NULL) {
		next_contactsmember = this_contactsmember->next;
		nm_free(this_contactsmember);
		this_contactsmember = next_contactsmember;
	}

	if (this_contactgroup->alias != this_contactgroup->group_name)
		nm_free(this_contactgroup->alias);
	nm_free(this_contactgroup->group_name);
	nm_free(this_contactgroup);
}

static void destroy_service(struct service *this_service)
{
	destroy_contactgroupsmember(this_service->contact_groups);
	destroy_contactsmember(this_service->contacts);
	destroy_customvars(this_service->custom_variables);

	if (this_service->display_name != this_service->description)
		nm_free(this_service->display_name);
	nm_free(this_service->description);
	nm_free(this_service->check_command);
	nm_free(this_service->plugin_output);
	nm_free(this_service->long_plugin_output);
	nm_free(this_service->perf_data);
	nm_free(this_service->event_handler_args);
	nm_free(this_service->check_command_args);
	free_objectlist(&this_service->servicegroups_ptr);
	free_objectlist(&this_service->notify_deps);
	free_objectlist(&this_service->exec_deps);
	free_objectlist(&this_service->escalation_list);
	nm_free(this_service->event_handler);
	nm_free(this_service->notes);
	nm_free(this_service->notes_url);
	nm_free(this_service->action_url);
	nm_free(this_service->icon_image);
	nm_free(this_service->icon_image_alt);
	nm_free(this_service);
}

static void destroy_serviceescalation(struct serviceescalation *this_serviceescalation)
{
	struct contactgroupsmember *this_contactgroupsmember;
	struct contactgroupsmember *next_contactgroupsmember;
	struct contactsmember *this_contactsmember, *next_contactsmember;

	/* free memory for the contact group members */
	this_contactgroupsmember = this_serviceescalation->contact_groups;
	while (this_contactgroupsmember != NULL) {
		next_contactgroupsmember = this_contactgroupsmember->next;
		nm_free(this_contactgroupsmember);
		this_contactgroupsmember = next_contactgroupsmember;
	}

	/* free memory for contacts */
	this_contactsmember = this_serviceescalation->contacts;
	while (this_contactsmember != NULL) {
		next_contactsmember = this_contactsmember->next;
		nm_free(this_contactsmember);
		this_contactsmember = next_contactsmember;
	}
	nm_free(this_serviceescalation);
}

static void destroy_hostescalation(struct hostescalation *this_hostescalation)
{
	struct contactgroupsmember *this_contactgroupsmember;
	struct contactgroupsmember *next_contactgroupsmember;
	struct contactsmember *this_contactsmember, *next_contactsmember;

	/* free memory for the contact group members */
	this_contactgroupsmember = this_hostescalation->contact_groups;
	while (this_contactgroupsmember != NULL) {
		next_contactgroupsmember = this_contactgroupsmember->next;
		nm_free(this_contactgroupsmember);
		this_contactgroupsmember = next_contactgroupsmember;
	}

	/* free memory for contacts */
	this_contactsmember = this_hostescalation->contacts;
	while (this_contactsmember != NULL) {
		next_contactsmember = this_contactsmember->next;
		nm_free(this_contactsmember);
		this_contactsmember = next_contactsmember;
	}
	nm_free(this_hostescalation);
}

static void destroy_servicedependency(struct servicedependency *svc_dep)
{
	free(svc_dep);
}

static void destroy_hostdependency(struct hostdependency *host_dep)
{
	free(host_dep);
}

/* free all allocated memory for objects */
int free_object_data(void)
{
	unsigned int i = 0;

	/*
	 * kill off hash tables so lingering modules don't look stuff up
	 * while we're busy removing it.
	 */
	for (i = 0; i < ARRAY_SIZE(object_hash_tables); i++) {
		dkhash_table *t = object_hash_tables[i];
		object_hash_tables[i] = NULL;
		dkhash_destroy(t);
	}

	/**** free memory for the host list ****/
	for (i = 0; i < num_objects.hosts; i++) {
		destroy_host(host_ary[i]);
	}
	nm_free(host_ary);

	/**** free memory for the host group list ****/
	for (i = 0; i < num_objects.hostgroups; i++) {
		destroy_hostgroup(hostgroup_ary[i]);
	}
	nm_free(hostgroup_ary);

	/**** free memory for the service group list ****/
	for (i = 0; i < num_objects.servicegroups; i++) {
		destroy_servicegroup(servicegroup_ary[i]);
	}
	nm_free(servicegroup_ary);

	/**** free memory for the contact group list ****/
	for (i = 0; i < num_objects.contactgroups; i++) {
		destroy_contactgroup(contactgroup_ary[i]);
	}
	nm_free(contactgroup_ary);

	/**** free memory for the service list ****/
	for (i = 0; i < num_objects.services; i++) {
		destroy_service(service_ary[i]);
	}
	nm_free(service_ary);

	/**** free service escalation memory ****/
	for (i = 0; i < num_objects.serviceescalations; i++) {
		destroy_serviceescalation(serviceescalation_ary[i]);
	}
	nm_free(serviceescalation_ary);

	/**** free service dependency memory ****/
	if (servicedependency_ary) {
		for (i = 0; i < num_objects.servicedependencies; i++)
			destroy_servicedependency(servicedependency_ary[i]);
		nm_free(servicedependency_ary);
	}

	/**** free host dependency memory ****/
	if (hostdependency_ary) {
		for (i = 0; i < num_objects.hostdependencies; i++)
			destroy_hostdependency(hostdependency_ary[i]);
		nm_free(hostdependency_ary);
	}

	/**** free host escalation memory ****/
	for (i = 0; i < num_objects.hostescalations; i++) {
		destroy_hostescalation(hostescalation_ary[i]);
	}
	nm_free(hostescalation_ary);

	/* we no longer have any objects */
	memset(&num_objects, 0, sizeof(num_objects));

	return OK;
}


/******************************************************************/
/*********************** CACHE FUNCTIONS **************************/
/******************************************************************/

void fcache_contactgrouplist(FILE *fp, const char *prefix, contactgroupsmember *list)
{
	if (list) {
		contactgroupsmember *l;
		fprintf(fp, "%s", prefix);
		for (l = list; l; l = l->next)
			fprintf(fp, "%s%c", l->group_name, l->next ? ',' : '\n');
	}
}

void fcache_hostlist(FILE *fp, const char *prefix, hostsmember *list)
{
	if (list) {
		hostsmember *l;
		fprintf(fp, "%s", prefix);
		for (l = list; l; l = l->next)
			fprintf(fp, "%s%c", l->host_name, l->next ? ',' : '\n');
	}
}

void fcache_contactgroup(FILE *fp, contactgroup *temp_contactgroup)
{
	fprintf(fp, "define contactgroup {\n");
	fprintf(fp, "\tcontactgroup_name\t%s\n", temp_contactgroup->group_name);
	if (temp_contactgroup->alias)
		fprintf(fp, "\talias\t%s\n", temp_contactgroup->alias);
	fcache_contactlist(fp, "\tmembers\t", temp_contactgroup->members);
	fprintf(fp, "\t}\n\n");
}

void fcache_hostgroup(FILE *fp, hostgroup *temp_hostgroup)
{
	fprintf(fp, "define hostgroup {\n");
	fprintf(fp, "\thostgroup_name\t%s\n", temp_hostgroup->group_name);
	if (temp_hostgroup->alias)
		fprintf(fp, "\talias\t%s\n", temp_hostgroup->alias);
	if (temp_hostgroup->members) {
		hostsmember *list;
		fprintf(fp, "\tmembers\t");
		for (list = temp_hostgroup->members; list; list = list->next)
			fprintf(fp, "%s%c", list->host_name, list->next ? ',' : '\n');
	}
	if (temp_hostgroup->notes)
		fprintf(fp, "\tnotes\t%s\n", temp_hostgroup->notes);
	if (temp_hostgroup->notes_url)
		fprintf(fp, "\tnotes_url\t%s\n", temp_hostgroup->notes_url);
	if (temp_hostgroup->action_url)
		fprintf(fp, "\taction_url\t%s\n", temp_hostgroup->action_url);
	fprintf(fp, "\t}\n\n");
}

void fcache_servicegroup(FILE *fp, servicegroup *temp_servicegroup)
{
	fprintf(fp, "define servicegroup {\n");
	fprintf(fp, "\tservicegroup_name\t%s\n", temp_servicegroup->group_name);
	if (temp_servicegroup->alias)
		fprintf(fp, "\talias\t%s\n", temp_servicegroup->alias);
	if (temp_servicegroup->members) {
		servicesmember *list;
		fprintf(fp, "\tmembers\t");
		for (list = temp_servicegroup->members; list; list = list->next) {
			service *s = list->service_ptr;
			fprintf(fp, "%s,%s%c", s->host_name, s->description, list->next ? ',' : '\n');
		}
	}
	if (temp_servicegroup->notes)
		fprintf(fp, "\tnotes\t%s\n", temp_servicegroup->notes);
	if (temp_servicegroup->notes_url)
		fprintf(fp, "\tnotes_url\t%s\n", temp_servicegroup->notes_url);
	if (temp_servicegroup->action_url)
		fprintf(fp, "\taction_url\t%s\n", temp_servicegroup->action_url);
	fprintf(fp, "\t}\n\n");
}

void fcache_host(FILE *fp, host *temp_host)
{
	fprintf(fp, "define host {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_host->name);
	if (temp_host->display_name != temp_host->name)
		fprintf(fp, "\tdisplay_name\t%s\n", temp_host->display_name);
	if (temp_host->alias)
		fprintf(fp, "\talias\t%s\n", temp_host->alias);
	if (temp_host->address)
		fprintf(fp, "\taddress\t%s\n", temp_host->address);
	fcache_hostlist(fp, "\tparents\t", temp_host->parent_hosts);
	if (temp_host->check_period)
		fprintf(fp, "\tcheck_period\t%s\n", temp_host->check_period);
	if (temp_host->check_command)
		fprintf(fp, "\tcheck_command\t%s\n", temp_host->check_command);
	if (temp_host->event_handler)
		fprintf(fp, "\tevent_handler\t%s\n", temp_host->event_handler);
	fcache_contactlist(fp, "\tcontacts\t", temp_host->contacts);
	fcache_contactgrouplist(fp, "\tcontact_groups\t", temp_host->contact_groups);
	if (temp_host->notification_period)
		fprintf(fp, "\tnotification_period\t%s\n", temp_host->notification_period);
	fprintf(fp, "\tinitial_state\t");
	if (temp_host->initial_state == STATE_DOWN)
		fprintf(fp, "d\n");
	else if (temp_host->initial_state == STATE_UNREACHABLE)
		fprintf(fp, "u\n");
	else
		fprintf(fp, "o\n");
	fprintf(fp, "\thourly_value\t%u\n", temp_host->hourly_value);
	fprintf(fp, "\tcheck_interval\t%f\n", temp_host->check_interval);
	fprintf(fp, "\tretry_interval\t%f\n", temp_host->retry_interval);
	fprintf(fp, "\tmax_check_attempts\t%d\n", temp_host->max_attempts);
	fprintf(fp, "\tactive_checks_enabled\t%d\n", temp_host->checks_enabled);
	fprintf(fp, "\tpassive_checks_enabled\t%d\n", temp_host->accept_passive_checks);
	fprintf(fp, "\tobsess\t%d\n", temp_host->obsess);
	fprintf(fp, "\tevent_handler_enabled\t%d\n", temp_host->event_handler_enabled);
	fprintf(fp, "\tlow_flap_threshold\t%f\n", temp_host->low_flap_threshold);
	fprintf(fp, "\thigh_flap_threshold\t%f\n", temp_host->high_flap_threshold);
	fprintf(fp, "\tflap_detection_enabled\t%d\n", temp_host->flap_detection_enabled);
	fprintf(fp, "\tflap_detection_options\t%s\n", opts2str(temp_host->flap_detection_options, host_flag_map, 'o'));
	fprintf(fp, "\tfreshness_threshold\t%d\n", temp_host->freshness_threshold);
	fprintf(fp, "\tcheck_freshness\t%d\n", temp_host->check_freshness);
	fprintf(fp, "\tnotification_options\t%s\n", opts2str(temp_host->notification_options, host_flag_map, 'r'));
	fprintf(fp, "\tnotifications_enabled\t%d\n", temp_host->notifications_enabled);
	fprintf(fp, "\tnotification_interval\t%f\n", temp_host->notification_interval);
	fprintf(fp, "\tfirst_notification_delay\t%f\n", temp_host->first_notification_delay);
	fprintf(fp, "\tstalking_options\t%s\n", opts2str(temp_host->stalking_options, host_flag_map, 'o'));
	fprintf(fp, "\tprocess_perf_data\t%d\n", temp_host->process_performance_data);
	if (temp_host->icon_image)
		fprintf(fp, "\ticon_image\t%s\n", temp_host->icon_image);
	if (temp_host->icon_image_alt)
		fprintf(fp, "\ticon_image_alt\t%s\n", temp_host->icon_image_alt);
	if (temp_host->vrml_image)
		fprintf(fp, "\tvrml_image\t%s\n", temp_host->vrml_image);
	if (temp_host->statusmap_image)
		fprintf(fp, "\tstatusmap_image\t%s\n", temp_host->statusmap_image);
	if (temp_host->have_2d_coords == TRUE)
		fprintf(fp, "\t2d_coords\t%d,%d\n", temp_host->x_2d, temp_host->y_2d);
	if (temp_host->have_3d_coords == TRUE)
		fprintf(fp, "\t3d_coords\t%f,%f,%f\n", temp_host->x_3d, temp_host->y_3d, temp_host->z_3d);
	if (temp_host->notes)
		fprintf(fp, "\tnotes\t%s\n", temp_host->notes);
	if (temp_host->notes_url)
		fprintf(fp, "\tnotes_url\t%s\n", temp_host->notes_url);
	if (temp_host->action_url)
		fprintf(fp, "\taction_url\t%s\n", temp_host->action_url);
	fprintf(fp, "\tretain_status_information\t%d\n", temp_host->retain_status_information);
	fprintf(fp, "\tretain_nonstatus_information\t%d\n", temp_host->retain_nonstatus_information);

	/* custom variables */
	fcache_customvars(fp, temp_host->custom_variables);
	fprintf(fp, "\t}\n\n");
}

void fcache_service(FILE *fp, service *temp_service)
{
	fprintf(fp, "define service {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_service->host_name);
	fprintf(fp, "\tservice_description\t%s\n", temp_service->description);
	if (temp_service->display_name != temp_service->description)
		fprintf(fp, "\tdisplay_name\t%s\n", temp_service->display_name);
	if (temp_service->parents) {
		fprintf(fp, "\tparents\t");
		/* same-host, single-parent? */
		if (!temp_service->parents->next && temp_service->parents->service_ptr->host_ptr == temp_service->host_ptr)
			fprintf(fp, "%s\n", temp_service->parents->service_ptr->description);
		else {
			servicesmember *sm;
			for (sm = temp_service->parents; sm; sm = sm->next) {
				fprintf(fp, "%s,%s%c", sm->host_name, sm->service_description, sm->next ? ',' : '\n');
			}
		}
	}
	if (temp_service->check_period)
		fprintf(fp, "\tcheck_period\t%s\n", temp_service->check_period);
	if (temp_service->check_command)
		fprintf(fp, "\tcheck_command\t%s\n", temp_service->check_command);
	if (temp_service->event_handler)
		fprintf(fp, "\tevent_handler\t%s\n", temp_service->event_handler);
	fcache_contactlist(fp, "\tcontacts\t", temp_service->contacts);
	fcache_contactgrouplist(fp, "\tcontact_groups\t", temp_service->contact_groups);
	if (temp_service->notification_period)
		fprintf(fp, "\tnotification_period\t%s\n", temp_service->notification_period);
	fprintf(fp, "\tinitial_state\t");
	if (temp_service->initial_state == STATE_WARNING)
		fprintf(fp, "w\n");
	else if (temp_service->initial_state == STATE_UNKNOWN)
		fprintf(fp, "u\n");
	else if (temp_service->initial_state == STATE_CRITICAL)
		fprintf(fp, "c\n");
	else
		fprintf(fp, "o\n");
	fprintf(fp, "\thourly_value\t%u\n", temp_service->hourly_value);
	fprintf(fp, "\tcheck_interval\t%f\n", temp_service->check_interval);
	fprintf(fp, "\tretry_interval\t%f\n", temp_service->retry_interval);
	fprintf(fp, "\tmax_check_attempts\t%d\n", temp_service->max_attempts);
	fprintf(fp, "\tis_volatile\t%d\n", temp_service->is_volatile);
	fprintf(fp, "\tactive_checks_enabled\t%d\n", temp_service->checks_enabled);
	fprintf(fp, "\tpassive_checks_enabled\t%d\n", temp_service->accept_passive_checks);
	fprintf(fp, "\tobsess\t%d\n", temp_service->obsess);
	fprintf(fp, "\tevent_handler_enabled\t%d\n", temp_service->event_handler_enabled);
	fprintf(fp, "\tlow_flap_threshold\t%f\n", temp_service->low_flap_threshold);
	fprintf(fp, "\thigh_flap_threshold\t%f\n", temp_service->high_flap_threshold);
	fprintf(fp, "\tflap_detection_enabled\t%d\n", temp_service->flap_detection_enabled);
	fprintf(fp, "\tflap_detection_options\t%s\n", opts2str(temp_service->flap_detection_options, service_flag_map, 'o'));
	fprintf(fp, "\tfreshness_threshold\t%d\n", temp_service->freshness_threshold);
	fprintf(fp, "\tcheck_freshness\t%d\n", temp_service->check_freshness);
	fprintf(fp, "\tnotification_options\t%s\n", opts2str(temp_service->notification_options, service_flag_map, 'r'));
	fprintf(fp, "\tnotifications_enabled\t%d\n", temp_service->notifications_enabled);
	fprintf(fp, "\tnotification_interval\t%f\n", temp_service->notification_interval);
	fprintf(fp, "\tfirst_notification_delay\t%f\n", temp_service->first_notification_delay);
	fprintf(fp, "\tstalking_options\t%s\n", opts2str(temp_service->stalking_options, service_flag_map, 'o'));
	fprintf(fp, "\tprocess_perf_data\t%d\n", temp_service->process_performance_data);
	if (temp_service->icon_image)
		fprintf(fp, "\ticon_image\t%s\n", temp_service->icon_image);
	if (temp_service->icon_image_alt)
		fprintf(fp, "\ticon_image_alt\t%s\n", temp_service->icon_image_alt);
	if (temp_service->notes)
		fprintf(fp, "\tnotes\t%s\n", temp_service->notes);
	if (temp_service->notes_url)
		fprintf(fp, "\tnotes_url\t%s\n", temp_service->notes_url);
	if (temp_service->action_url)
		fprintf(fp, "\taction_url\t%s\n", temp_service->action_url);
	fprintf(fp, "\tretain_status_information\t%d\n", temp_service->retain_status_information);
	fprintf(fp, "\tretain_nonstatus_information\t%d\n", temp_service->retain_nonstatus_information);

	/* custom variables */
	fcache_customvars(fp, temp_service->custom_variables);
	fprintf(fp, "\t}\n\n");
}

void fcache_servicedependency(FILE *fp, servicedependency *temp_servicedependency)
{
	fprintf(fp, "define servicedependency {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_servicedependency->host_name);
	fprintf(fp, "\tservice_description\t%s\n", temp_servicedependency->service_description);
	fprintf(fp, "\tdependent_host_name\t%s\n", temp_servicedependency->dependent_host_name);
	fprintf(fp, "\tdependent_service_description\t%s\n", temp_servicedependency->dependent_service_description);
	if (temp_servicedependency->dependency_period)
		fprintf(fp, "\tdependency_period\t%s\n", temp_servicedependency->dependency_period);
	fprintf(fp, "\tinherits_parent\t%d\n", temp_servicedependency->inherits_parent);
	fprintf(fp, "\t%s_failure_options\t%s\n",
	        temp_servicedependency->dependency_type == NOTIFICATION_DEPENDENCY ? "notification" : "execution",
	        opts2str(temp_servicedependency->failure_options, service_flag_map, 'o'));
	fprintf(fp, "\t}\n\n");
}

void fcache_serviceescalation(FILE *fp, serviceescalation *temp_serviceescalation)
{
	fprintf(fp, "define serviceescalation {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_serviceescalation->host_name);
	fprintf(fp, "\tservice_description\t%s\n", temp_serviceescalation->description);
	fprintf(fp, "\tfirst_notification\t%d\n", temp_serviceescalation->first_notification);
	fprintf(fp, "\tlast_notification\t%d\n", temp_serviceescalation->last_notification);
	fprintf(fp, "\tnotification_interval\t%f\n", temp_serviceescalation->notification_interval);
	if (temp_serviceescalation->escalation_period)
		fprintf(fp, "\tescalation_period\t%s\n", temp_serviceescalation->escalation_period);
	fprintf(fp, "\tescalation_options\t%s\n", opts2str(temp_serviceescalation->escalation_options, service_flag_map, 'r'));

	if (temp_serviceescalation->contacts) {
		contactsmember *cl;
		fprintf(fp, "\tcontacts\t");
		for (cl = temp_serviceescalation->contacts; cl; cl = cl->next)
			fprintf(fp, "%s%c", cl->contact_ptr->name, cl->next ? ',' : '\n');
	}
	if (temp_serviceescalation->contact_groups) {
		contactgroupsmember *cgl;
		fprintf(fp, "\tcontact_groups\t");
		for (cgl = temp_serviceescalation->contact_groups; cgl; cgl = cgl->next)
			fprintf(fp, "%s%c", cgl->group_name, cgl->next ? ',' : '\n');
	}
	fprintf(fp, "\t}\n\n");
}

void fcache_hostdependency(FILE *fp, hostdependency *temp_hostdependency)
{
	fprintf(fp, "define hostdependency {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_hostdependency->host_name);
	fprintf(fp, "\tdependent_host_name\t%s\n", temp_hostdependency->dependent_host_name);
	if (temp_hostdependency->dependency_period)
		fprintf(fp, "\tdependency_period\t%s\n", temp_hostdependency->dependency_period);
	fprintf(fp, "\tinherits_parent\t%d\n", temp_hostdependency->inherits_parent);
	fprintf(fp, "\t%s_failure_options\t%s\n",
	        temp_hostdependency->dependency_type == NOTIFICATION_DEPENDENCY ? "notification" : "execution",
	        opts2str(temp_hostdependency->failure_options, host_flag_map, 'o'));
	fprintf(fp, "\t}\n\n");
}

void fcache_hostescalation(FILE *fp, hostescalation *temp_hostescalation)
{
	fprintf(fp, "define hostescalation {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_hostescalation->host_name);
	fprintf(fp, "\tfirst_notification\t%d\n", temp_hostescalation->first_notification);
	fprintf(fp, "\tlast_notification\t%d\n", temp_hostescalation->last_notification);
	fprintf(fp, "\tnotification_interval\t%f\n", temp_hostescalation->notification_interval);
	if (temp_hostescalation->escalation_period)
		fprintf(fp, "\tescalation_period\t%s\n", temp_hostescalation->escalation_period);
	fprintf(fp, "\tescalation_options\t%s\n", opts2str(temp_hostescalation->escalation_options, host_flag_map, 'r'));

	fcache_contactlist(fp, "\tcontacts\t", temp_hostescalation->contacts);
	fcache_contactgrouplist(fp, "\tcontact_groups\t", temp_hostescalation->contact_groups);
	fprintf(fp, "\t}\n\n");
}

/* writes cached object definitions for use by web interface */
int fcache_objects(char *cache_file)
{
	FILE *fp = NULL;
	time_t current_time = 0L;
	unsigned int i;

	/* some people won't want to cache their objects */
	if (!cache_file || !strcmp(cache_file, "/dev/null"))
		return OK;

	time(&current_time);

	/* open the cache file for writing */
	fp = fopen(cache_file, "w");
	if (fp == NULL) {
		nm_log(NSLOG_CONFIG_WARNING, "Warning: Could not open object cache file '%s' for writing!\n", cache_file);
		return ERROR;
	}

	/* write header to cache file */
	fprintf(fp, "########################################\n");
	fprintf(fp, "#       NAGIOS OBJECT CACHE FILE\n");
	fprintf(fp, "#\n");
	fprintf(fp, "# THIS FILE IS AUTOMATICALLY GENERATED\n");
	fprintf(fp, "# BY NAGIOS.  DO NOT MODIFY THIS FILE!\n");
	fprintf(fp, "#\n");
	fprintf(fp, "# Created: %s", ctime(&current_time));
	fprintf(fp, "########################################\n\n");


	/* cache timeperiods */
	for (i = 0; i < num_objects.timeperiods; i++)
		fcache_timeperiod(fp, timeperiod_ary[i]);

	/* cache commands */
	for (i = 0; i < num_objects.commands; i++)
		fcache_command(fp, command_ary[i]);

	/* cache contactgroups */
	for (i = 0; i < num_objects.contactgroups; i++)
		fcache_contactgroup(fp, contactgroup_ary[i]);

	/* cache hostgroups */
	for (i = 0; i < num_objects.hostgroups; i++)
		fcache_hostgroup(fp, hostgroup_ary[i]);

	/* cache servicegroups */
	for (i = 0; i < num_objects.servicegroups; i++)
		fcache_servicegroup(fp, servicegroup_ary[i]);

	/* cache contacts */
	for (i = 0; i < num_objects.contacts; i++)
		fcache_contact(fp, contact_ary[i]);

	/* cache hosts */
	for (i = 0; i < num_objects.hosts; i++)
		fcache_host(fp, host_ary[i]);

	/* cache services */
	for (i = 0; i < num_objects.services; i++)
		fcache_service(fp, service_ary[i]);

	/* cache service dependencies */
	for (i = 0; i < num_objects.servicedependencies; i++)
		fcache_servicedependency(fp, servicedependency_ary[i]);

	/* cache service escalations */
	for (i = 0; i < num_objects.serviceescalations; i++)
		fcache_serviceescalation(fp, serviceescalation_ary[i]);

	/* cache host dependencies */
	for (i = 0; i < num_objects.hostdependencies; i++)
		fcache_hostdependency(fp, hostdependency_ary[i]);

	/* cache host escalations */
	for (i = 0; i < num_objects.hostescalations; i++)
		fcache_hostescalation(fp, hostescalation_ary[i]);

	fclose(fp);

	return OK;
}
