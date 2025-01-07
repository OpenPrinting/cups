/*
 * Online help CGI for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2011 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cgi-private.h"


/*
 * 'main()' - Main entry for CGI.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  help_index_t	*hi,			/* Help index */
		*si;			/* Search index */
  help_node_t	*n;			/* Current help node */
  int		i;			/* Looping var */
  const char	*query = NULL;		/* Search query */
  const char	*cache_dir;		/* CUPS_CACHEDIR environment variable */
  const char	*docroot;		/* CUPS_DOCROOT environment variable */
  const char	*helpfile,		/* Current help file */
		*helptitle = NULL;	/* Current help title */
  const char	*topic;			/* Current topic */
  char		topic_data[1024];	/* Topic form data */
  const char	*section;		/* Current section */
  char		filename[1024],		/* Filename */
		directory[1024];	/* Directory */
  cups_file_t	*fp;			/* Help file */
  char		line[1024];		/* Line from file */
  int		printable;		/* Show printable version? */


 /*
  * Get any form variables...
  */

  cgiInitialize();

  printable = cgiGetVariable("PRINTABLE") != NULL;

 /*
  * Set the web interface section...
  */

  cgiSetVariable("SECTION", "help");
  cgiSetVariable("REFRESH_PAGE", "");

 /*
  * Load the help index...
  */

  if ((cache_dir = getenv("CUPS_CACHEDIR")) == NULL)
    cache_dir = CUPS_CACHEDIR;

  snprintf(filename, sizeof(filename), "%s/help.index", cache_dir);

  if ((docroot = getenv("CUPS_DOCROOT")) == NULL)
    docroot = CUPS_DOCROOT;

  snprintf(directory, sizeof(directory), "%s/help", docroot);

  fprintf(stderr, "DEBUG: helpLoadIndex(filename=\"%s\", directory=\"%s\")\n",
          filename, directory);

  hi = helpLoadIndex(filename, directory);
  if (!hi)
  {
    perror(filename);

    cgiStartHTML(cgiText(_("Online Help")));
    cgiSetVariable("ERROR", cgiText(_("Unable to load help index.")));
    cgiCopyTemplateLang("error.tmpl");
    cgiEndHTML();

    return (1);
  }

  fprintf(stderr, "DEBUG: %d nodes in help index...\n",
          cupsArrayCount(hi->nodes));

 /*
  * See if we are viewing a file...
  */

  for (i = 0; i < argc; i ++)
    fprintf(stderr, "DEBUG: argv[%d]=\"%s\"\n", i, argv[i]);

  if ((helpfile = getenv("PATH_INFO")) != NULL)
  {
    helpfile ++;

    if (!*helpfile)
      helpfile = NULL;
  }

  if (helpfile)
  {
   /*
    * Verify that the help file exists and is part of the index...
    */

    snprintf(filename, sizeof(filename), "%s/help/%s", docroot, helpfile);

    fprintf(stderr, "DEBUG: helpfile=\"%s\", filename=\"%s\"\n",
            helpfile, filename);

    if (access(filename, R_OK))
    {
      perror(filename);

      cgiStartHTML(cgiText(_("Online Help")));
      cgiSetVariable("ERROR", cgiText(_("Unable to access help file.")));
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();

      return (1);
    }

    if ((n = helpFindNode(hi, helpfile, NULL)) == NULL)
    {
      cgiStartHTML(cgiText(_("Online Help")));
      cgiSetVariable("ERROR", cgiText(_("Help file not in index.")));
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();

      return (1);
    }

   /*
    * Save the page title and help file...
    */

    helptitle = n->text;
    topic     = n->section;

   /*
    * Send a standard page header...
    */

    if (printable)
      puts("Content-Type: text/html;charset=utf-8\n");
    else
      cgiStartHTML(n->text);
  }
  else
  {
   /*
    * Send a standard page header...
    */

    cgiStartHTML(cgiText(_("Online Help")));

    topic = cgiGetVariable("TOPIC");
  }

 /*
  * Do a search as needed...
  */

  if (cgiGetVariable("CLEAR"))
    cgiSetVariable("QUERY", "");
  else if ((query = cgiGetTextfield("QUERY")) != NULL)
    query = strdup(query);

  si    = helpSearchIndex(hi, query, topic, helpfile);

  cgiClearVariables();
  if (query)
    cgiSetVariable("QUERY", query);
  if (topic)
    cgiSetVariable("TOPIC", topic);
  if (helpfile)
    cgiSetVariable("HELPFILE", helpfile);
  if (helptitle)
    cgiSetVariable("HELPTITLE", helptitle);

  fprintf(stderr, "DEBUG: query=\"%s\", topic=\"%s\"\n",
          query ? query : "(null)", topic ? topic : "(null)");

  if (si)
  {
    help_node_t	*nn;			/* Parent node */


    fprintf(stderr,
            "DEBUG: si=%p, si->sorted=%p, cupsArrayCount(si->sorted)=%d\n", si,
            si->sorted, cupsArrayCount(si->sorted));

    for (i = 0, n = (help_node_t *)cupsArrayFirst(si->sorted);
         n;
	 i ++, n = (help_node_t *)cupsArrayNext(si->sorted))
    {
      if (helpfile && n->anchor)
        snprintf(line, sizeof(line), "#%s", n->anchor);
      else if (n->anchor)
        snprintf(line, sizeof(line), "/help/%s?QUERY=%s#%s", n->filename,
	         query ? query : "", n->anchor);
      else
        snprintf(line, sizeof(line), "/help/%s?QUERY=%s", n->filename,
	         query ? query : "");

      cgiSetArray("QTEXT", i, n->text);
      cgiSetArray("QLINK", i, line);

      if (!helpfile && n->anchor)
      {
        nn = helpFindNode(hi, n->filename, NULL);

        snprintf(line, sizeof(line), "/help/%s?QUERY=%s", nn->filename,
	         query ? query : "");

        cgiSetArray("QPTEXT", i, nn->text);
	cgiSetArray("QPLINK", i, line);
      }
      else
      {
        cgiSetArray("QPTEXT", i, "");
	cgiSetArray("QPLINK", i, "");
      }

      fprintf(stderr, "DEBUG: [%d] = \"%s\" @ \"%s\"\n", i, n->text, line);
    }

    helpDeleteIndex(si);
  }

 /*
  * OK, now list the bookmarks within the index...
  */

  for (i = 0, section = NULL, n = (help_node_t *)cupsArrayFirst(hi->sorted);
       n;
       n = (help_node_t *)cupsArrayNext(hi->sorted))
  {
    if (n->anchor)
      continue;

   /*
    * Add a section link as needed...
    */

    if (n->section &&
        (!section || strcmp(n->section, section)))
    {
     /*
      * Add a link for this node...
      */

      snprintf(line, sizeof(line), "/help/?TOPIC=%s&QUERY=%s",
               cgiFormEncode(topic_data, n->section, sizeof(topic_data)),
	       query ? query : "");
      cgiSetArray("BMLINK", i, line);
      cgiSetArray("BMTEXT", i, n->section);
      cgiSetArray("BMINDENT", i, "0");

      i ++;
      section = n->section;
    }

    if (!topic || !n->section || strcmp(n->section, topic))
      continue;

   /*
    * Add a link for this node...
    */

    snprintf(line, sizeof(line), "/help/%s?TOPIC=%s&QUERY=%s", n->filename,
             cgiFormEncode(topic_data, n->section, sizeof(topic_data)),
	     query ? query : "");
    cgiSetArray("BMLINK", i, line);
    cgiSetArray("BMTEXT", i, n->text);
    cgiSetArray("BMINDENT", i, "1");

    i ++;

    if (helpfile && !strcmp(helpfile, n->filename))
    {
      help_node_t	*nn;		/* Pointer to sub-node */


      cupsArraySave(hi->sorted);

      for (nn = (help_node_t *)cupsArrayFirst(hi->sorted);
           nn;
	   nn = (help_node_t *)cupsArrayNext(hi->sorted))
        if (nn->anchor && !strcmp(helpfile, nn->filename))
	{
	 /*
	  * Add a link for this node...
	  */

	  snprintf(line, sizeof(line), "#%s", nn->anchor);
	  cgiSetArray("BMLINK", i, line);
	  cgiSetArray("BMTEXT", i, nn->text);
	  cgiSetArray("BMINDENT", i, "2");

	  i ++;
	}

      cupsArrayRestore(hi->sorted);
    }
  }

 /*
  * Show the search and bookmark content...
  */

  if (!helpfile || !printable)
    cgiCopyTemplateLang("help-header.tmpl");
  else
    cgiCopyTemplateLang("help-printable.tmpl");

 /*
  * If we are viewing a file, copy it in now...
  */

  if (helpfile)
  {
    if ((fp = cupsFileOpen(filename, "r")) != NULL)
    {
      int	inbody;			/* Are we inside the body? */
      char	*lineptr;		/* Pointer into line */

      inbody = 0;

      while (cupsFileGets(fp, line, sizeof(line)))
      {
        for (lineptr = line; *lineptr && isspace(*lineptr & 255); lineptr ++);

        if (inbody)
	{
	  if (!_cups_strncasecmp(lineptr, "</BODY>", 7))
	    break;

	  printf("%s\n", line);
        }
	else if (!_cups_strncasecmp(lineptr, "<BODY", 5))
	  inbody = 1;
      }

      cupsFileClose(fp);
    }
    else
    {
      perror(filename);
      cgiSetVariable("ERROR", cgiText(_("Unable to open help file.")));
      cgiCopyTemplateLang("error.tmpl");
    }
  }

 /*
  * Send a standard trailer...
  */

  if (!printable)
  {
    cgiCopyTemplateLang("help-trailer.tmpl");
    cgiEndHTML();
  }
  else
    puts("</BODY>\n</HTML>");

 /*
  * Delete the index...
  */

  helpDeleteIndex(hi);

 /*
  * Return with no errors...
  */

  return (0);
}
