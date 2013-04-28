#!/usr/bin/perl
#perl xpjp.pl -v 1 -t xcode -c Debug -c Release -p Macosx32 -d includePipeline=false -d includeUICLibs=true -d includeViewer=true -x UICViewer.xpj -o UICViewer
#perl xpjp.pl -v 1 -t xcode -c Debug -c Release -p Macosx32 -d LibraryCompileTemplate=StaticLibNoWarnings -d DynamicLibraryCompileTemplate=DynamicLibNoWarnings -x UICLibs.xpj

use strict;

use File::Basename;
use File::Spec::Functions;
use Cwd qw(realpath getcwd);
use XML::LibXML;
use File::Path;
use Data::UUID;
use FindBin;
use xpjpom;

#globals required by various functions
my $currentdir = "";


#template variables for this template instantiation
my $tempvar;
#xpj variables for this xpj run
my $xpj;
my $templates = {};
my $xpj_stack;

sub find_directory {
  my $startdir = shift;
  my $target = shift;
  my $combined = catdir( $startdir, $target );
  while( ( length($startdir) > 3 ) && !(-e $combined) ) {
	$startdir = dirname($startdir);
	$combined = catdir( $startdir, $target );
  }
  return realpath( $startdir );
}

sub eval_buildconf_file {
  my $fullpath = shift;
  my $document = do {
    local $/ = undef;
    open my $fh, "<", $fullpath
        or die "could not open file: $!";
    <$fh>;
  };
  my @include;
  my $environment;
  my $requires;
  my $aquaConfig;
  my $platforms;
  my $buildConfig;
  eval( $document );
  my $hash_ref = {
                    'environment' => $environment,
                    'aquaConfig'  => $aquaConfig,
                    'buildConfig' => $buildConfig,
				    'platforms' => $platforms,
                  };
  return $hash_ref;
}

sub get_buildconf_var {
  my $environment = shift;
  my $platformVars = shift;
  my $varname = uc( shift );
  
  if ( (exists $platformVars->{$varname} ) ) {
	return $platformVars->{$varname};
  }
  if ( (exists $environment->{$varname} ) ) {
	return $environment->{$varname};
  }
  die "Unrecognized environment variable: $varname\n";
}


sub parse_xpj_templates_element {
  my $xmlelem = shift;
  my $elemName = $xmlelem->nodeName;
  my @childnodes = $xmlelem->childNodes;
  if ( $elemName eq "template" ) {
	if ( $xmlelem->hasAttribute( "filename" ) ) {
	  my $fname = $xmlelem->getAttribute( "filename" );
	  my $templatePath = realpath( catfile( $currentdir, $fname ) );
	  do {
		print "parsing template file $templatePath\n";
		my $old_current_dir = $currentdir;
		$currentdir = dirname( $templatePath );
		my $doc;
		open my $fh, '<', $templatePath or die 'could not open template file $templatePath';
		binmode $fh; # drop all PerlIO layers possibly created by a use open pragma
		$doc = XML::LibXML->load_xml(IO => $fh);
		parse_xpj_templates_doc($doc);
		$currentdir = $old_current_dir;
	  };
	}
	else {
	  my $templateName = uc($xmlelem->getAttribute( "name" ));
	  $templates->{$templateName} = $xmlelem;
	}
  }
  elsif( $elemName eq "perl" ) {	  
	  my $elemText = "";
	  foreach my $child (@childnodes) {
		  if ( $child->nodeType == XML_TEXT_NODE ) {
			  $elemText = $elemText . $child->data();
		  }
	  }
	  eval( $elemText );
	  die "Failed to eval $elemText: $@" if $@;
  }
  elsif( $elemName eq "target" ) {
	  $xpj->{target_name} = $xmlelem->getAttribute( "name" );
  }

  foreach my $child (@childnodes) {
	if ( $child->nodeType == XML_ELEMENT_NODE ) {
	  parse_xpj_templates_element( $child, $templates );
	}
  }
}

sub parse_xpj_templates_doc {
  my $xmldoc = shift;
  my $topElem = $xmldoc->documentElement();
  parse_xpj_templates_element( $topElem );
}

sub eval_xpj_string_data {
  my $strdata = shift;
  my $last_match_end = 0;
  my $str_end = length($strdata);
  my $retval = "";
  my $strmatch = "";
  
  #iterate through the string and produce a new string
  #performing the required substitutions for the variables
  while( ($strmatch) = $strdata =~ m/\{\{\[(.*?)\]\}\}/g ) {
    my $match_end_pos = $+[0];
	my $match_start_pos = $-[0];
	my $match_len = $match_end_pos - $match_start_pos;
	my $nonmatched = substr( $strdata, 0, $match_start_pos );
	$strdata = substr( $strdata, $match_end_pos );
	my $result = eval( $strmatch );
	die "Execution of $strmatch failed: $@" if $@;
	$retval = $retval . $nonmatched . $result;
  }
  if ( $last_match_end != $str_end ) {
	$retval = $retval . substr( $strdata, $last_match_end, $str_end - $last_match_end );
  }
  return $retval;
}

sub eval_xpj_copy_attributes {
  my $sourceElem = shift;
  my $destElem = shift;
  my @srcAtts = $sourceElem->attributes;
  my $sourceName = $sourceElem->nodeName;
  foreach my $att (@srcAtts) {
	my $attName = $att->nodeName;
	my $attValue = eval_xpj_string_data( $att->value );
	$destElem->setAttribute( $attName, $attValue );
  }
}

sub eval_xpj_apply_template {
  my $source_elem = shift;
  my $dest_elem = shift;
  my $evalStr = $source_elem->getAttribute("name");
  my $template_name = uc(eval_xpj_string_data($evalStr));
  if ( (exists $templates->{$template_name} ) ) {
	my $existing_template_variables = $tempvar;
	my $new_template_variables = {};
	my @apply_children = $source_elem->childNodes;
	foreach my $apply_child (@apply_children ) {
	  if ( $apply_child->nodeType == XML_ELEMENT_NODE ) {
		my $apply_child_name = $apply_child->nodeName;
		if ( $apply_child_name eq "define" ) {
		  my $apply_key = eval_xpj_string_data( $apply_child->getAttribute( "key" ) );
		  my $apply_value = eval_xpj_string_data( $apply_child->getAttribute( "value" ) );
		  $new_template_variables->{$apply_key} = $apply_value;
		}
		elsif( $apply_child_name eq "forward-defines" ) {
			foreach my $hashkey (keys %$existing_template_variables) {
				$new_template_variables->{$hashkey} = $existing_template_variables->{$hashkey};
			}
		}
		else {
		  die "Unrecognized apply-template child name $apply_child_name";
		}
	  }
	}
	$tempvar = $new_template_variables;
	my $template = $templates->{$template_name};
	my @template_children = $template->childNodes;
	foreach my $template_child (@template_children) {
	  if ( $template_child->nodeType == XML_ELEMENT_NODE ) {
		eval_xpj_element( $template_child, $dest_elem );
	  }
	}
	$tempvar = $existing_template_variables;
  }
  else {
	die "Unrecognized template name: $template_name\n";
  }
}

sub eval_xpj_copy_element {
  #source is the element we are evaluating
  my $source_elem = shift;
  #dest is the element we are evaluating *into*
  my $dest_elem = shift;
  
  my $dest_doc = $dest_elem->ownerDocument;
  my $new_elem = $dest_doc->createElement( $source_elem->nodeName );
  $dest_elem->appendChild( $new_elem );
  eval_xpj_copy_attributes($source_elem, $new_elem );
  my @source_children = $source_elem->childNodes;
  foreach my $source_child (@source_children) {
	if ( $source_child->nodeType == XML_ELEMENT_NODE ) {
	  eval_xpj_element( $source_child, $new_elem );
	}
	elsif( $source_child->nodeType == XML_TEXT_NODE ) {
	  my $new_text_data = eval_xpj_string_data( $source_child->data );
	  $new_elem->appendText( $new_text_data );
	}
  }
}

sub eval_xpj_element {
  #source is the element we are evaluating
  my $source_elem = shift;
  #dest is the element we are evaluating *into*
  my $dest_elem = shift;
  my $source_name = lc($source_elem->nodeName);
  #already processed this
  if( $source_name eq 'template' ) {
  }
  elsif( $source_name eq 'perl' ) {
  }
  elsif( $source_name eq 'apply-template' ) {
	eval_xpj_apply_template( $source_elem, $dest_elem );
  }
  else {
	eval_xpj_copy_element( $source_elem, $dest_elem );
  }
}

#evaluate a dom document producing a new dom document with
#all of the possible variables filled in.
#in general we are just copying input to output *except*
#we are going through all content and apply templates *and*
#evaluating conditions and filling in variables.
sub eval_xpj_file {
  my $xmldoc = shift;
  my $newdoc = XML::LibXML::Document->new( "1", "utf-8" );
  my $new_doc_elem = $newdoc->createElement( "xpjp" );
  my $source_doc_element = $xmldoc->documentElement;
  $newdoc->setDocumentElement( $new_doc_elem );

  eval_xpj_copy_attributes( $source_doc_element, $new_doc_elem );
  my @source_children = $source_doc_element->childNodes;
  foreach my $source_child (@source_children) {
	if ( $source_child->nodeType == XML_ELEMENT_NODE ) {
	  eval_xpj_element( $source_child, $new_doc_elem );
	}
  }
  return $newdoc;
}


$xpj = {};
$xpj->{'tool'} = "vc12";
$xpj->{'platform'} = 'Win32';

my $parser = XML::LibXML->new({ line_numbers => 1 });
my $doc = $parser->parse_file( 'cclj.xpjp' );
$templates = {};
parse_xpj_templates_doc($doc, $templates);
my $newdoc = eval_xpj_file( $doc );
$newdoc->toFile( 'cclj.xpjp.expand', 1 );

my $toolname = $xpj->{'tool'};
#require the backend and see what happens
my $toolmodule = require "$toolname.pm";
my $om = new xpjpom;

$om->extend_compilation_properties( $toolmodule->{extended_compilation_properties} );
$om->build_object_model( $newdoc );
$toolmodule->{process}( $om, $xpj );

1;




#parse the command line parameters
#sample invocation: 
#-v 1 -t $vcver -c Debug -c Release -p $platform -x UICLibs.xpj

# my $verbose = 0;
# my $xpjtool = "";
# my @configurations = ();
# my $defines = {};
# my $xpjfile = "";
# my $outputfile = "";
# my $platform = "";

# my $argcount = @ARGV;



# for ( my $argi = 0; $argi < $argcount; ++$argi ) {
#   my $currentArg = $ARGV[$argi];
#   if ( $currentArg eq "-v" ) {
# 	++$argi;
# 	my $arg = $ARGV[$argi];
# 	if ( $arg eq "1" ) {
# 	  $verbose = 1;
# 	}
#   }
#   elsif( $currentArg eq "-c" ) {
# 	++$argi;
# 	push( @configurations, $ARGV[$argi] );	
#   }
#   elsif( $currentArg eq "-p" ) {
# 	++$argi;
# 	$platform = $ARGV[$argi];
#   }
#   elsif( $currentArg eq "-t" ) {
# 	++$argi;
# 	$xpjtool = $ARGV[$argi];
#   }
#   elsif ( $currentArg eq "-d" ) {
# 	++$argi;
# 	my ($varname, $varvalue ) = $ARGV[$argi] =~ /(\w+)=(\w+)/;
# 	$defines->{uc($varname)} = $varvalue;
#   }
#   elsif ( $currentArg eq "-x" ) {
# 	++$argi;
# 	$xpjfile = catfile( getcwd(), $ARGV[$argi] )
#   }
#   elsif( $currentArg eq "-o" ) {
# 	++$argi;
# 	$outputfile = $ARGV[$argi];
#   }
# }

# die "Unable to find xpj file $xpjfile" unless -e $xpjfile;
# $xpjfile = realpath( $xpjfile );
# $currentdir = dirname( $xpjfile );

# my $projectdir = find_directory( $currentdir, "Architect" );
# my $uicdir = find_directory($projectdir, "Tools/boost" );
# my $toolsdir = catdir( $uicdir, "Tools" );
# print "project directory: $projectdir\ntools directory: $toolsdir\n";
# my $buildconf = catfile( $projectdir, "buildconf.pl" );

# $ENV{PROJECT_ROOT} = $projectdir;
# $ENV{TOOLS_ROOT} = $uicdir;

# my $variables = eval_buildconf_file( $buildconf );
# my $env = upcase_hash( $variables->{'environment'} );
# my $uictopdir = $env->{'UIC_TOP_DIR'};
# my $platforms = $variables->{'platforms'};
# my $platform_hashes = upcase_hash( $variables->{'platforms'} );
# die "Undefined platform $platform\n" unless exists $platform_hashes->{uc($platform)};
# my $platformEnv = upcase_hash( $platform_hashes->{uc($platform)}->{'environment'} );

# print "parsing xpj file: $xpjfile\n";
# my $doc;
# do {
#   open my $fh, '<', $xpjfile or die 'could not open xpj file';
#   binmode $fh; # drop all PerlIO layers possibly created by a use open pragma
#   $doc = XML::LibXML->load_xml(IO => $fh);
# };

# #template references
# my $templates = {};

# #navigate the document pulling templates from the document.

# parse_xpj_templates_doc($doc, $templates);


# #now we want to produce a new document by running through the original document,
# #expanding all variable references and all template declarations.  This allows us
# #to save out the resulting document and ensure that our template engine is correct.
# #it also allows us to use a simpler engine to answer queries about the xpj dataset.

# #xpj has three variable sources.  The first is the environment, the second is predefined xpj vars
# #and the third is variables defined on the command line.  


# my $xpjvars = {
# 	platform => $platform,
#     tool => $xpjtool,
# };


# my $xpj_eval_context = {
#    templates => $templates,
#    xpj_variables => $xpjvars,
# };


# my $newdoc = eval_xpj_file( $xpj_eval_context, $doc );
# my $testfile = $xpjfile . ".expand";
# $newdoc->toFile( $testfile, 1 );

1;
