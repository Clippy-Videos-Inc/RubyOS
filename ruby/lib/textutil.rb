# /lib/textutil.rb - exemplo de biblioteca carregada com  require "textutil"
#
# Qualquer arquivo .rb em /lib pode ser carregado assim (uma unica vez por programa).

module TextUtil
  # Quebra o texto em linhas de ate width colunas (so nos espacos).
  def self.wrap(text, width = 40)
    lines = []
    line = ""
    text.split(" ").each do |word|
      if line.empty?
        line = word
      elsif line.size + 1 + word.size <= width
        line = line + " " + word
      else
        lines << line
        line = word
      end
    end
    lines << line unless line.empty?
    lines
  end

  def self.title(text)
    text.split(" ").map { |w| w[0].upcase + w[1, w.size - 1].downcase }.join(" ")
  end

  def self.box(text)
    width = text.size + 2
    ["+" + "-" * width + "+", "| " + text + " |", "+" + "-" * width + "+"]
  end
end
