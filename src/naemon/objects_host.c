#include "objects_host.h"
#include "objects_contactgroup.h"
#include "objects_contact.h"
#include "objects_timeperiod.h"
#include "objects.h"
#include "objectlist.h"
#include "logging.h"
#include "nm_alloc.h"

#include <string.h>

static dkhash_table *host_hash_table = NULL;
host *host_list = NULL;
host **host_ary = NULL;

int init_objects_host(int elems)
{
	if (!elems) {
		host_ary = NULL;
		host_hash_table = NULL;
		return ERROR;
	}
	host_ary = nm_calloc(elems, sizeof(host*));
	host_hash_table = dkhash_create(elems * 1.5);
	return OK;
}

void destroy_objects_host()
{
	unsigned int i;
	for (i = 0; i < num_objects.hosts; i++) {
		host *this_host = host_ary[i];
		destroy_host(this_host);
	}
	host_list = NULL;
	dkhash_destroy(host_hash_table);
	nm_free(host_ary);
	num_objects.hosts = 0;
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
		result = dkhash_insert(host_hash_table, new_host->name, NULL, new_host);
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
	else
		host_list = new_host;
	return new_host;
}

void destroy_host(host *this_host)
{
	struct hostsmember *this_hostsmember, *next_hostsmember;
	struct servicesmember *this_servicesmember, *next_servicesmember;
	struct contactgroupsmember *this_contactgroupsmember, *next_contactgroupsmember;
	struct contactsmember *this_contactsmember, *next_contactsmember;
	struct customvariablesmember *this_customvariablesmember, *next_customvariablesmember;

	/* free memory for parent hosts */
	this_hostsmember = this_host->parent_hosts;
	while (this_hostsmember != NULL) {
		next_hostsmember = this_hostsmember->next;
		nm_free(this_hostsmember->host_name);
		nm_free(this_hostsmember);
		this_hostsmember = next_hostsmember;
	}

	/* free memory for child host links */
	this_hostsmember = this_host->child_hosts;
	while (this_hostsmember != NULL) {
		next_hostsmember = this_hostsmember->next;
		nm_free(this_hostsmember);
		this_hostsmember = next_hostsmember;
	}

	/* free memory for service links */
	this_servicesmember = this_host->services;
	while (this_servicesmember != NULL) {
		next_servicesmember = this_servicesmember->next;
		nm_free(this_servicesmember);
		this_servicesmember = next_servicesmember;
	}

	/* free memory for contact groups */
	this_contactgroupsmember = this_host->contact_groups;
	while (this_contactgroupsmember != NULL) {
		next_contactgroupsmember = this_contactgroupsmember->next;
		nm_free(this_contactgroupsmember);
		this_contactgroupsmember = next_contactgroupsmember;
	}

	/* free memory for contacts */
	this_contactsmember = this_host->contacts;
	while (this_contactsmember != NULL) {
		next_contactsmember = this_contactsmember->next;
		nm_free(this_contactsmember);
		this_contactsmember = next_contactsmember;
	}

	/* free memory for custom variables */
	this_customvariablesmember = this_host->custom_variables;
	while (this_customvariablesmember != NULL) {
		next_customvariablesmember = this_customvariablesmember->next;
		nm_free(this_customvariablesmember->variable_name);
		nm_free(this_customvariablesmember->variable_value);
		nm_free(this_customvariablesmember);
		this_customvariablesmember = next_customvariablesmember;
	}

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

host *find_host(const char *name)
{
	return dkhash_get(host_hash_table, name, NULL);
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
	/* duplicate string vars */
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

int get_host_count(void)
{
	return num_objects.hosts;
}

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

int is_host_immediate_parent_of_host(host *child_host, host *parent_host)
{

	if (is_host_immediate_child_of_host(parent_host, child_host) == TRUE)
		return TRUE;

	return FALSE;
}

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

static void fcache_hostlist(FILE *fp, const char *prefix, hostsmember *list)
{
	if (list) {
		hostsmember *l;
		fprintf(fp, "%s", prefix);
		for (l = list; l; l = l->next)
			fprintf(fp, "%s%c", l->host_name, l->next ? ',' : '\n');
	}
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
