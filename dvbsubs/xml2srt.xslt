<?xml version="1.0" encoding="iso-8859-1"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<xsl:output method="text" encoding="iso-8859-1" />

<xsl:template match="/">
<xsl:apply-templates select="//spu"/>
</xsl:template>

<xsl:template match="spu"><xsl:value-of select="position()"/><xsl:text>
</xsl:text><xsl:value-of select="translate(@start,'.',',')"/> --> <xsl:value-of select="translate(@end,'.',',')"/><xsl:text>
</xsl:text>
<xsl:apply-templates/><xsl:text>
</xsl:text>
</xsl:template>

<xsl:template match="line"><xsl:value-of select="."/><xsl:text>
</xsl:text></xsl:template>
<xsl:template match="text()"></xsl:template>

</xsl:stylesheet>
