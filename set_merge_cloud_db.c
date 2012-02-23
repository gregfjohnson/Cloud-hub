/* set_merge_cloud_db.c - init cloud mesh parameters based on nvram settings
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: set_merge_cloud_db.c,v 1.8 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: set_merge_cloud_db.c,v 1.8 2012-02-22 18:55:25 greg Exp $";

/* a boot-time routine that is given nvram values on its command line,
 * and sets up initialization options for merge_cloud before it is
 * started based on those nvram values.
 */

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "util.h"

static void add_buf(char *buf, char *add)
{
    if (strlen(buf) == 0) {
        sprintf(buf, "d %s", add);
    } else {
        sprintf(buf, "%s,%s", buf, add);
    }
}

int main(int argc, char **argv)
{
    bool_t had_cloud_db = false;
    bool_t changed_cloud_db = false;
    char buf[256];
    FILE *f;

    f = fopen("/tmp/cloud.db", "r");
    if (f != NULL) {
        had_cloud_db = true;
        fgets(buf, 256, f);
        buf[strlen(buf) - 1] = '\0';
        fclose(f);
    } else {
        buf[0] = '\0';
    }

    /* it is enabled in merge_cloud.c initially.
     * gui "enabled" <--> argv[2] == 1, so do nothing in that case.
     * gui "disabled" <--> argv[2] == 0, so output to cloud.db to toggle it.
     */
    if (strcmp(argv[1], "mesh_flow_control") == 0
        && strcmp(argv[2], "0") == 0)
    {
        add_buf(buf, "22");
        changed_cloud_db = true;
    }

    /* it is enabled in merge_cloud.c initially.
     * gui "enabled" <--> argv[2] == 1, so do nothing in that case.
     * gui "disabled" <--> argv[2] == 0, so output to cloud.db to toggle it.
     */
    if (strcmp(argv[1], "mesh_comm_traffic") == 0
        && strcmp(argv[2], "0") == 0)
    {
        add_buf(buf, "36");
        changed_cloud_db = true;
    }

    /* it is disabled in merge_cloud.c initially.
     * gui "enabled" <--> argv[2] == 1, so output to cloud.db to toggle it.
     * gui "disabled" <--> argv[2] == 0, so do nothing in that case.
     */
    if (strcmp(argv[1], "mesh_show_topology") == 0
        && strcmp(argv[2], "1") == 0)
    {
        add_buf(buf, "38");
        changed_cloud_db = true;
    }

    /* it is enabled in merge_cloud.c initially.
     * gui "enabled" <--> argv[2] == 1, so do nothing in that case.
     * gui "disabled" <--> argv[2] == 0, so output to cloud.db to toggle it.
     */
    if (strcmp(argv[1], "mesh_ad_hoc_clients") == 0
        && strcmp(argv[2], "0") == 0)
    {
        add_buf(buf, "50");
        changed_cloud_db = true;
    }

    if (changed_cloud_db) {
        char tmp_name[256];
        sprintf(tmp_name, "/tmp/cloud.db.%d", getpid());
        f = fopen(tmp_name, "w");
        fprintf(f, "%s\n", buf);
        fclose(f);
        rename(tmp_name, "/tmp/cloud.db");
    }

    return 0;
}
