/*
 * Home page CGI for CUPS.
 *
 * Copyright © 2025 by OpenPrinting.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "cgi-private.h"
#include <errno.h>


/*
 * Local functions...
 */

static void	do_dashboard(void);
static void	do_search(char *query);


/*
 * 'main()' - Main entry for CGI.
 */

int					/* O - Exit status */
main(void)
{
  char	*query;				/* Query string, if any */


 /*
  * Get any form variables...
  */

  cgiInitialize();

 /*
  * Set the web interface section...
  */

  cgiSetVariable("SECTION", "home");
  cgiSetVariable("REFRESH_PAGE", "");

 /*
  * Show the home page...
  */

  cgiStartHTML(cgiText(_("Home")));

  if ((query = cgiGetVariable("QUERY")) != NULL)
    do_search(query);
  else
    do_dashboard();

  cgiEndHTML();

 /*
  * Return with no errors...
  */

  return (0);
}


/*
 * 'do_dashboard()' - Show the home page dashboard...
 */

static void
do_dashboard(void)
{
  cgiCopyTemplateLang("home.tmpl");
}


/*
 * 'do_search()' - Search classes, printers, jobs, and online help.
 */

static void
do_search(char *query)			/* I - Search string */
{
  (void)query;
}
